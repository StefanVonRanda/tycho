/* tychoc - the Tycho compiler.
 *
 * Pipeline:  .ty source -> tokens (indentation-aware) -> AST -> type
 * resolution -> C source (with the Tycho runtime embedded verbatim) ->
 * invoke `cc` to produce a native binary.
 *
 * Usage:
 *   tychoc file.ty [-o name] [--emit-c] [--cc <compiler>]
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
#include <limits.h>
#include <dirent.h>
#include <unistd.h>

#include "tycho_rt_embed.h"   /* defines: static const char *TYCHO_RUNTIME */

/* ------------------------------------------------------------------ util */

static const char *g_srcname = "<input>";
static const char *g_src = NULL;   /* current file's source text, for the error snippet (set in lex) */
static int g_line_info = 0;        /* -g: emit `#line N "src.ty"` before each statement (single-file only) */
static char *g_line_file = NULL;   /* the source path, C-string-escaped, for those directives */
static int g_err_col = 0;          /* 1-based caret column (0 = none); set from the offending token before die_at */

__attribute__((noreturn, format(printf, 2, 3)))
static void die_at(int line, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "%s:%d: error: ", g_srcname, line);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    /* show the offending source line under the message (single-file: always the
     * right file; package mode: only when `line` lands in the last-lexed file),
     * and a caret under the offending token when its column is known. */
    if (g_src && line > 0) {
        const char *p = g_src; int ln = 1;
        while (ln < line && *p) { if (*p++ == '\n') ln++; }
        if (ln == line && *p && *p != '\n') {
            const char *eol = p; while (*eol && *eol != '\n') eol++;
            fprintf(stderr, "  %4d | %.*s\n", line, (int)(eol - p), p);
            if (g_err_col > 0 && g_err_col <= (eol - p) + 1)
                fprintf(stderr, "       | %*s^\n", g_err_col - 1, "");
        }
    }
    exit(1);
}

/* Like die_at but non-fatal: a `<file>:<line>: warning: ...` diagnostic (+ source
 * snippet) that the language server parses the same way it parses errors. */
__attribute__((format(printf, 2, 3)))
static void warn_at(int line, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "%s:%d: warning: ", g_srcname, line);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    if (g_src && line > 0) {
        const char *p = g_src; int ln = 1;
        while (ln < line && *p) { if (*p++ == '\n') ln++; }
        if (ln == line && *p && *p != '\n') {
            const char *eol = p; while (*eol && *eol != '\n') eol++;
            fprintf(stderr, "  %4d | %.*s\n", line, (int)(eol - p), p);
        }
    }
}

static char *sfmt(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    char *s = NULL;
    if (vasprintf(&s, fmt, ap) < 0 || !s) { fprintf(stderr, "tychoc: oom\n"); exit(1); }
    va_end(ap);
    return s;
}

static void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) { fprintf(stderr, "tychoc: oom\n"); exit(1); }
    return p;
}

static void *xrealloc(void *p, size_t n) {
    void *r = realloc(p, n);
    if (!r) { fprintf(stderr, "tychoc: oom\n"); exit(1); }
    return r;
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
    TK_IDENT, TK_INT, TK_FLOAT, TK_STR, TK_CHAR,
    TK_COLONCOLON, TK_COLONEQ, TK_COLON, TK_EQ,
    TK_EQEQ, TK_NEQ, TK_LT, TK_GT, TK_LE, TK_GE,
    TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH, TK_PERCENT,
    TK_PIPE, TK_CARET, TK_TILDE, TK_SHL, TK_SHR,
    TK_LPAREN, TK_RPAREN, TK_LBRACKET, TK_RBRACKET, TK_COMMA, TK_ARROW,
    TK_FN, TK_RETURN, TK_IF, TK_ELIF, TK_ELSE, TK_FOR, TK_IN, TK_TRUE, TK_FALSE, TK_NULL, TK_STRUCT,
    TK_INOUT, TK_AMP, TK_AND, TK_OR, TK_NOT, TK_MATCH, TK_ENUM, TK_ORRETURN, TK_TYPE, TK_HANDLE,
    TK_BREAK, TK_CONTINUE,
    TK_SPAWN, TK_PARALLEL, TK_SELECT,
    TK_DOT, TK_DOLLAR,
    TK_KW_INT, TK_KW_BOOL, TK_KW_STRING, TK_KW_FLOAT, TK_KW_PTR, TK_KW_BYTES,
    TK_KW_U32, TK_KW_U64, TK_KW_F32
} TokKind;

typedef struct {
    TokKind kind;
    char   *text;   /* identifier name, or raw string contents */
    long    ival;
    int     line;
    double  fval;   /* TK_FLOAT literal value */
    int     col;    /* 1-based column of the token start, for the error caret (0 = unknown) */
} Tok;

typedef struct {
    Tok *v;
    int  n, cap;
} TokVec;

static void tv_push(TokVec *t, Tok tok) {
    if (t->n == t->cap) {
        t->cap = t->cap ? t->cap * 2 : 64;
        t->v = (Tok *)realloc(t->v, (size_t)t->cap * sizeof(Tok));
        if (!t->v) { fprintf(stderr, "tychoc: oom\n"); exit(1); }
    }
    t->v[t->n++] = tok;
}

/* After a value-producing token, a '.' is field/tuple access (t.0, x.field), so
 * `.5` there is NOT a leading-dot float -- it stays DOT + INT. Everywhere else
 * (after an operator, '(', '[', ',', '=', a keyword, or at line start) a '.'
 * before a digit begins a float. */
static int tok_postfixable(int k) {
    return k == TK_IDENT || k == TK_INT || k == TK_FLOAT || k == TK_STR
        || k == TK_CHAR || k == TK_RPAREN || k == TK_RBRACKET;
}

static TokKind keyword(const char *s) {
    if (!strcmp(s, "fn"))     return TK_FN;
    if (!strcmp(s, "return")) return TK_RETURN;
    if (!strcmp(s, "if"))     return TK_IF;
    if (!strcmp(s, "elif"))   return TK_ELIF;
    if (!strcmp(s, "else"))   return TK_ELSE;
    if (!strcmp(s, "and"))    return TK_AND;
    if (!strcmp(s, "or_return")) return TK_ORRETURN;   /* before "or": longer match wins anyway, but explicit */
    if (!strcmp(s, "or"))     return TK_OR;
    if (!strcmp(s, "not"))    return TK_NOT;
    if (!strcmp(s, "match"))  return TK_MATCH;
    if (!strcmp(s, "break"))    return TK_BREAK;
    if (!strcmp(s, "continue")) return TK_CONTINUE;
    if (!strcmp(s, "spawn"))    return TK_SPAWN;
    if (!strcmp(s, "parallel")) return TK_PARALLEL;
    if (!strcmp(s, "select"))   return TK_SELECT;
    if (!strcmp(s, "for"))    return TK_FOR;
    if (!strcmp(s, "in"))     return TK_IN;
    if (!strcmp(s, "struct")) return TK_STRUCT;
    if (!strcmp(s, "enum"))   return TK_ENUM;
    if (!strcmp(s, "handle")) return TK_HANDLE;
    if (!strcmp(s, "type"))   return TK_TYPE;
    if (!strcmp(s, "mut"))  return TK_INOUT;
    if (!strcmp(s, "true"))   return TK_TRUE;
    if (!strcmp(s, "false"))  return TK_FALSE;
    if (!strcmp(s, "null"))   return TK_NULL;
    if (!strcmp(s, "int"))    return TK_KW_INT;
    if (!strcmp(s, "bool"))   return TK_KW_BOOL;
    if (!strcmp(s, "string")) return TK_KW_STRING;
    if (!strcmp(s, "float"))  return TK_KW_FLOAT;
    if (!strcmp(s, "ptr"))    return TK_KW_PTR;
    if (!strcmp(s, "bytes"))  return TK_KW_BYTES;
    if (!strcmp(s, "u32"))    return TK_KW_U32;
    if (!strcmp(s, "u64"))    return TK_KW_U64;
    if (!strcmp(s, "f32"))    return TK_KW_F32;
    return TK_IDENT;
}

/* Indentation-aware lexer. Processes the source line by line so blank
 * lines and comment-only lines never affect the indent stack. */
static TokVec lex(const char *src) {
    g_src = src;   /* for die_at's source-line snippet (re-set per file in package mode) */
    TokVec out = {0};
    int indent_stack[256];
    int sp = 0;
    indent_stack[0] = 0;
    int line = 0;

    const char *p = src;
    while (*p) {
        line++;
        const char *ls = p;                 /* line start */
        /* Leading whitespace: tabs OR spaces, but never a mix within one
         * line (the single ambiguous case). Each leading char counts as one
         * indent unit, so a consistently-indented file -- all tabs or all
         * spaces -- nests correctly; the indent stack compares depths, not
         * absolute display widths. */
        int col = 0, ws_sp = 0, ws_tab = 0;
        while (*p == ' ' || *p == '\t') {
            if (*p == ' ') ws_sp = 1; else ws_tab = 1;
            col++; p++;
        }
        if (ws_sp && ws_tab)
            die_at(line, "mixed tabs and spaces in indentation; use one consistently");

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
            tv_push(&out, (Tok){TK_INDENT, NULL, 0, line, 0, 0});
        } else {
            while (col < indent_stack[sp]) {
                sp--;
                tv_push(&out, (Tok){TK_DEDENT, NULL, 0, line, 0, 0});
            }
            if (col != indent_stack[sp])
                die_at(line, "inconsistent indentation");
        }

        /* lex the rest of the line */
        (void)ls;
        while (*p && *p != '\n' && *p != '#') {
            char c = *p;
            if (c == ' ' || c == '\t' || c == '\r') { p++; continue; }
            int tcol = (int)(p - ls) + 1;   /* token start column (1-based), for the error caret */

            int lead_dot = (c == '.' && isdigit((unsigned char)p[1]) &&
                            (out.n == 0 || !tok_postfixable(out.v[out.n - 1].kind)));
            if (isdigit((unsigned char)c) || lead_dot) {
                const char *s = p;
                while (isdigit((unsigned char)*p)) p++;   /* integer part (empty for .5) */
                /* a '.' immediately followed by a digit makes it a float (D.D);
                 * an `e`/`E` exponent (optionally signed) does too, with or
                 * without a fractional part (1e10, 1.5e-3, 2E8). A bare trailing
                 * '.' stays a field token, and a malformed exponent (1e, 1.e5,
                 * 1e+) is NOT consumed -- the 'e...' lexes as a separate ident.
                 * No leading-dot form. */
                int is_float = 0;
                if (*p == '.' && isdigit((unsigned char)p[1])) {
                    is_float = 1;
                    p++;
                    while (isdigit((unsigned char)*p)) p++;
                }
                if (*p == 'e' || *p == 'E') {
                    const char *eq = p + 1;
                    if (*eq == '+' || *eq == '-') eq++;
                    if (isdigit((unsigned char)*eq)) {
                        is_float = 1;
                        p = eq;
                        while (isdigit((unsigned char)*p)) p++;
                    }
                }
                if (is_float) {
                    double dv = strtod(s, NULL);
                    tv_push(&out, (Tok){TK_FLOAT, NULL, 0, line, dv, tcol});
                } else {
                    long v = 0;
                    for (const char *q = s; q < p; q++) {
                        int d = *q - '0';
                        if (v > (LONG_MAX - d) / 10) die_at(line, "integer literal out of range");
                        v = v * 10 + d;
                    }
                    tv_push(&out, (Tok){TK_INT, NULL, v, line, 0, tcol});
                }
                continue;
            }
            if ((isalpha((unsigned char)c) || c == '_') && !(c == 'f' && p[1] == '"')) {   /* f"..." is an interpolated string, not an identifier */
                const char *s = p;
                while (isalnum((unsigned char)*p) || *p == '_') p++;
                char *name = xstrndup(s, (size_t)(p - s));
                TokKind k = keyword(name);
                tv_push(&out, (Tok){k, name, 0, line, 0, tcol});
                continue;
            }
            if (c == '"' || (c == 'f' && p[1] == '"')) {
                int interp = (c == 'f');   /* f"..." -> an interpolated string (ival flag) */
                if (interp) p++;           /* skip the f prefix */
                p++;                       /* skip the opening quote */
                /* keep raw contents; Tycho escapes (\n \t \\ \") are a
                 * subset of C escapes so they pass straight through to
                 * the generated C string literal. */
                char buf[4096];
                int bn = 0;
                int depth = 0;             /* brace depth inside an f-string `{...}` hole (0 in the literal part) */
                while (*p && (*p != '"' || depth > 0)) {
                    if (*p == '\n') die_at(line, "unterminated string literal");
                    if (interp && depth == 0 && p[0] == '{' && p[1] == '{') {   /* literal {{ — stays out of a hole */
                        if (bn + 2 >= (int)sizeof buf) die_at(line, "string too long");
                        buf[bn++] = *p++; buf[bn++] = *p++; continue;
                    }
                    if (interp && depth == 0 && p[0] == '}' && p[1] == '}') {   /* literal }} */
                        if (bn + 2 >= (int)sizeof buf) die_at(line, "string too long");
                        buf[bn++] = *p++; buf[bn++] = *p++; continue;
                    }
                    if (interp && *p == '{') {            /* open / nest a hole */
                        if (bn + 1 >= (int)sizeof buf) die_at(line, "string too long");
                        depth++; buf[bn++] = *p++; continue;
                    }
                    if (interp && depth > 0 && *p == '}') {  /* close a hole level */
                        if (bn + 1 >= (int)sizeof buf) die_at(line, "string too long");
                        depth--; buf[bn++] = *p++; continue;
                    }
                    if (interp && depth > 0 && *p == '"') {   /* nested string literal inside a hole: copy verbatim */
                        if (bn + 1 >= (int)sizeof buf) die_at(line, "string too long");
                        buf[bn++] = *p++;                     /* opening quote */
                        while (*p && *p != '"') {
                            if (*p == '\n') die_at(line, "unterminated string literal");
                            if (*p == '\\') {
                                if (bn + 2 >= (int)sizeof buf) die_at(line, "string too long");
                                buf[bn++] = *p++;
                                if (!*p) die_at(line, "unterminated string literal");
                                buf[bn++] = *p++;             /* hole code is re-lexed later — don't validate escapes here */
                            } else {
                                if (bn + 1 >= (int)sizeof buf) die_at(line, "string too long");
                                buf[bn++] = *p++;
                            }
                        }
                        if (*p != '"') die_at(line, "unterminated string literal");
                        if (bn + 1 >= (int)sizeof buf) die_at(line, "string too long");
                        buf[bn++] = *p++;                     /* closing quote */
                        continue;
                    }
                    if (*p == '\\') {
                        if (bn + 2 >= (int)sizeof buf) die_at(line, "string too long");
                        buf[bn++] = *p++;
                        if (!*p) die_at(line, "unterminated string literal");
                        char e = *p;
                        if (e != 'n' && e != 't' && e != '\\' && e != '"')
                            die_at(line, "unsupported escape \\%c (use \\n \\t \\\\ \\\")", e);
                        buf[bn++] = *p++;
                    } else {
                        /* reject raw control bytes in the literal text (tab
                         * excepted): otherwise they are emitted verbatim into
                         * the generated C string literal — a raw CR/NUL/etc.
                         * can corrupt or break out of it. Newline is already
                         * rejected above; require an escape for the rest. */
                        if (depth == 0 && (unsigned char)*p < 0x20 && *p != '\t')
                            die_at(line, "raw control byte in string literal (use an escape such as \\n or \\t)");
                        if (bn + 1 >= (int)sizeof buf) die_at(line, "string too long");
                        buf[bn++] = *p++;
                    }
                }
                if (*p != '"') die_at(line, "unterminated string literal");
                p++;
                tv_push(&out, (Tok){TK_STR, xstrndup(buf, (size_t)bn), interp, line, 0, tcol});
                continue;
            }

            if (c == '\'') {        /* char literal: 'x' or one escape -> one byte */
                p++;
                long cv;
                if (*p == '\\') {
                    p++;
                    switch (*p) {
                        case 'n':  cv = '\n'; break;
                        case 't':  cv = '\t'; break;
                        case 'r':  cv = '\r'; break;
                        case '0':  cv = '\0'; break;
                        case '\\': cv = '\\'; break;
                        case '\'': cv = '\''; break;
                        default: die_at(line, "unsupported char escape (use \\n \\t \\r \\0 \\\\ \\')");
                    }
                    p++;
                } else if (*p && *p != '\'' && *p != '\n') {
                    cv = (unsigned char)*p;
                    p++;
                } else {
                    die_at(line, "empty or unterminated char literal");
                }
                if (*p != '\'') die_at(line, "char literal must be exactly one character");
                p++;
                tv_push(&out, (Tok){TK_CHAR, NULL, cv, line, 0, tcol});
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
            else if (c == '<' && c2 == '<') { k = TK_SHL;        len = 2; }
            else if (c == '>' && c2 == '>') { k = TK_SHR;        len = 2; }
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
            else if (c == '%') k = TK_PERCENT;
            else if (c == '|') k = TK_PIPE;
            else if (c == '^') k = TK_CARET;
            else if (c == '~') k = TK_TILDE;
            else if (c == '$') k = TK_DOLLAR;   /* generics: `$T` introduces a type parameter */
            else { g_err_col = tcol; die_at(line, "unexpected character '%c'", c); }
            tv_push(&out, (Tok){k, NULL, 0, line, 0, tcol});
            p += len;
        }

        tv_push(&out, (Tok){TK_NEWLINE, NULL, 0, line, 0, (int)(p - ls) + 1});
        if (*p == '#') while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }

    /* close out remaining indentation, then EOF */
    while (sp > 0) { sp--; tv_push(&out, (Tok){TK_DEDENT, NULL, 0, line, 0, 0}); }
    tv_push(&out, (Tok){TK_EOF, NULL, 0, line, 0, 0});
    return out;
}

/* ------------------------------------------------------------------ AST */

/* Type is an int so a struct id can be encoded in it: values >=
 * T_STRUCT_BASE name a struct (id = value - base). The primitive enum
 * constants keep working in every existing == and switch. */
typedef int Type;
enum { T_VOID, T_INT, T_BOOL, T_STRING, T_ARRAY_INT, T_ARRAY_STRING, T_MAP_SI, T_FLOAT, T_ARRAY_FLOAT,
       T_MAP_SF /* [string: float] */,
       T_MAP_II /* [int: int] */, T_MAP_IF /* [int: float] */,
       T_NONE, /* type of a bare `None` until context fixes its concrete Option type */
       /* Ok(v)/Err(e) each know only ONE of Result's two type params, so they
        * carry a partial sentinel (the known inner type sits on the value's lhs)
        * until context fixes the full Result type — the same trick as T_NONE. */
       T_OK_PARTIAL, T_ERR_PARTIAL,
       T_CHAR, /* one byte; represented as `long` in C, prints as a char via string append */
       T_PTR, /* FFI opaque handle: void* in C. No deref/arithmetic in tycho — only pass to C, compare to null, is_null */
       T_BYTES, /* immutable byte buffer: same length-headered char* repr as string (so all string runtime ops
                 * reuse directly), but a DISTINCT type that crosses FFI as (ptr,len) / out-param, not char*. */
       T_U32, /* first-class 32-bit unsigned: C `unsigned int`, wraps at 2^32 natively (crypto/bit-twiddling) */
       T_U64, /* first-class 64-bit unsigned: C `unsigned long long`, wraps at 2^64 natively */
       T_F32, /* first-class 32-bit float: C `float` */
       T_PENDING /* B-3 (bidirectional inference): a bare `xs := []` / `x := None` decl, awaiting its
                  * first grounding use in the same block; never survives resolve */ };
#define T_STRUCT_BASE   64
/* structs occupy [64, T_ARRC_BASE); composite arrays sit above that (both are
 * >= 64, so the upper bound is what keeps an array type from looking like a
 * struct). */
#define IS_STRUCT(t)    ((t) >= T_STRUCT_BASE && (t) < T_ARRC_BASE)
#define STRUCT_ID(t)    ((int)((t) - T_STRUCT_BASE))
#define STRUCT_TYPE(id) (T_STRUCT_BASE + (id))

/* Dynamic compiler tables. A scaling registry is a heap buffer that doubles on
 * demand, so program size (functions, vars, types, ...) is bounded by memory,
 * not a fixed cap. Reads stay `g_X[i]`; only an APPEND needs TBL_ENSURE first.
 * Tables start NULL/cap 0; first growth allocates 16 (xrealloc, above, exits on
 * OOM). NOTE: a held `&g_X[i]` must NOT span a growth of the same table X (it
 * would dangle) -- the one place that stored such a pointer (g_sigs, via
 * g_spawn / ParFor.sig) stores an index instead. Type-id-encoded tables
 * (structs/enums/array/...) additionally cap at the id-range gap to the next
 * T_*_BASE (see those sites) -- the encoding ceiling, far above any real cap. */
#define TBL_ENSURE(tbl, n, cap) do { \
    if ((n) >= (cap)) { (cap) = (cap) ? (cap) * 2 : 16; \
        (tbl) = xrealloc((tbl), (size_t)(cap) * sizeof *(tbl)); } \
} while (0)
/* Reserve capacity for at least `need` total elements up front (for a run of
 * appends with no per-item ENSURE, e.g. the builtin signatures). */
#define TBL_RESERVE(tbl, need, cap) do { \
    if ((cap) < (need)) { int _c = (cap) ? (cap) : 16; while (_c < (need)) _c *= 2; \
        (cap) = _c; (tbl) = xrealloc((tbl), (size_t)(cap) * sizeof *(tbl)); } \
} while (0)

typedef struct { char *name; Type type; } Field;
typedef struct { char *name; Field *fields; int nfields; int fields_cap; int line;
                 int generic; Type typarams[8]; int ntyparams;
                 int from_tmpl; Type from_args[8]; int nfrom_args; } StructDef;   /* generics: `struct Box($T)` template; instances are concrete copies with $T substituted. from_tmpl>=0 records the template+args this instance came from (for matching a recursive self-reference). */
static StructDef *g_structs;
static int g_nstructs = 0, g_structs_cap = 0;
static int struct_find(const char *name) {
    for (int i = 0; i < g_nstructs; i++)
        if (!strcmp(g_structs[i].name, name)) return i;
    return -1;
}

/* Task(T) — the handle `spawn f(args)` returns; wait(t) consumes it. A task
 * has NO source-level type syntax (it can only be held in a local inferred by
 * `let`), so it can never appear in a param, return type, or struct field;
 * the intern-time guards below close the remaining container routes. The C
 * representation is an opaque `HTask *` (runtime struct: thread id + the
 * task's private root arena + the result slot). Copying the handle word is a
 * plain alias -- affine (exactly-one-wait) enforcement is CC-2. */
#define T_TASK_BASE 53248   /* above the function-type range (49152 + 256) */
typedef struct { Type inner; } TaskType;
static TaskType *g_tasktypes;
static int g_ntasktypes = 0, g_tasktypes_cap = 0;
#define IS_TASK(t) ((t) >= T_TASK_BASE && (t) < T_CHAN_BASE)
#define TASK_ID(t) ((int)((t) - T_TASK_BASE))
static Type task_of(Type inner) {                /* find-or-create Task(inner) */
    for (int i = 0; i < g_ntasktypes; i++)
        if (g_tasktypes[i].inner == inner) return T_TASK_BASE + i;
    if (g_ntasktypes >= 1024) { fprintf(stderr, "tychoc: too many task types\n"); exit(1); }   /* T_CHAN_BASE - T_TASK_BASE (defined below) */
    TBL_ENSURE(g_tasktypes, g_ntasktypes, g_tasktypes_cap);
    g_tasktypes[g_ntasktypes].inner = inner;
    return T_TASK_BASE + g_ntasktypes++;
}
static Type task_inner(Type t) { return g_tasktypes[TASK_ID(t)].inner; }
/* A task that escapes into a container could be waited twice or never while
 * aliased -- fail closed at the type-intern choke points (every aggregate
 * containing a task would have to intern a type through one of these). */
static void task_container_err(void) {
    fprintf(stderr, "tychoc: a task handle cannot be stored in a container or aggregate -- wait(t) first\n");
    exit(1);
}

/* Typed C handles (FFI R2, docs/internals/typed-handles-design.md): `handle Name:
 * free: free_fn` declares an affine, opaque (void*) type that the compiler frees
 * by emitting `free_fn(h)` at the owning variable's scope exit -- the same affine
 * + finalizer model as tasks, with a user-supplied destructor. Sits in the free
 * range between channels (54272+4096) and typarams (65536). Defined before the
 * container-intern functions so they can fail closed on a contained handle. */
#define T_HANDLE_BASE 58368
#define IS_HANDLE(t) ((t) >= T_HANDLE_BASE && (t) < T_HANDLE_BASE + 256)
#define HANDLE_ID(t) ((int)((t) - T_HANDLE_BASE))
typedef struct { const char *name; const char *free_fn; int line; } HandleType;
static HandleType g_handles[256];
static int g_nhandles = 0;
static int handle_find(const char *name) {
    for (int i = 0; i < g_nhandles; i++) if (!strcmp(g_handles[i].name, name)) return i;
    return -1;
}
static void handle_container_err(void) {
    fprintf(stderr, "tychoc: a handle cannot be stored in a container/aggregate, captured, or returned -- it is freed at the end of its scope\n");
    exit(1);
}

/* Channel(T) -- the bounded queue from `ch := channel(T, cap)` (CC-4). The
 * ONE shared object in tycho concurrency: the C representation is `HChan *`
 * and copying the handle word ALIASES it on purpose (it is internally
 * synchronized; send deep-copies in, recv deep-copies out, so values stay
 * value-semantic). Has type syntax (a spawned worker takes `Channel(T)`),
 * but may not be returned, stored in containers/aggregates, or captured by
 * closures -- the handle must not outlive its creating scope, which frees
 * it after CC-2's implicit joins. */
#define T_CHAN_BASE 54272   /* above the task range (53248 + 64) */
typedef struct { Type inner; } ChanType;
static ChanType *g_chantypes;
static int g_nchantypes = 0, g_chantypes_cap = 0;
#define IS_CHAN(t) ((t) >= T_CHAN_BASE && (t) < T_CHAN_BASE + 4096)
#define CHAN_ID(t) ((int)((t) - T_CHAN_BASE))
static void chan_container_err(void) {
    fprintf(stderr, "tychoc: a channel handle cannot be stored in a container or aggregate -- pass it as an argument instead\n");
    exit(1);
}
static Type chan_of(Type inner) {                /* find-or-create Channel(inner) */
    if (IS_TASK(inner)) task_container_err();
    if (IS_HANDLE(inner)) handle_container_err();
    if (IS_CHAN(inner)) chan_container_err();
    for (int i = 0; i < g_nchantypes; i++)
        if (g_chantypes[i].inner == inner) return T_CHAN_BASE + i;
    if (g_nchantypes >= 4096) { fprintf(stderr, "tychoc: too many channel types\n"); exit(1); }
    TBL_ENSURE(g_chantypes, g_nchantypes, g_chantypes_cap);
    g_chantypes[g_nchantypes].inner = inner;
    return T_CHAN_BASE + g_nchantypes++;
}
static Type chan_inner(Type t) { return g_chantypes[CHAN_ID(t)].inner; }

/* Composite array types — arrays whose element is a struct or another array
 * ([Point], [[int]], ...). Unlike [int]/[float]/[string] (fixed enum values
 * with hand-written runtime), these are interned in a side table (mirroring
 * struct interning) and their runtime type + ops are GENERATED, one monomorphic
 * TychoArrC<id> per distinct element type used. Ids start above the struct
 * range; the element is interned before its container, so id order is a valid
 * emit order. */
#define T_ARRC_BASE 1024
#define T_OPT_BASE  4096   /* defined here so IS_ARRC's upper bound can reference it */
#define T_RES_BASE  6144   /* Result(T,E), between the Option and enum ranges */
#define T_ENUM_BASE 8192   /* user sum types, above the Result range */
#define T_TUP_BASE  16384  /* tuples (T1, ..., Tn), above the (now bounded) enum range */
#define T_NT_BASE   24576  /* distinct newtypes (type X = int/float), above tuples */
#define T_MAPC_BASE 32768  /* composite maps [K: V] with an arbitrary value type, above newtypes */
#define T_TYPARAM_BASE 65536  /* generics: `$T` type parameters — transient (only in generic templates), bound to a concrete type at instantiation, never reach codegen */
typedef struct { Type elem; } ArrType;
static ArrType *g_arrtypes;
static int g_narrtypes = 0, g_arrtypes_cap = 0;
#define IS_ARRC(t)  ((t) >= T_ARRC_BASE && (t) < T_OPT_BASE)   /* options sit above */
#define ARRC_ID(t)  ((int)((t) - T_ARRC_BASE))
static Type arrc_of(Type elem) {                 /* find-or-create [elem] */
    if (IS_TASK(elem)) task_container_err();
    if (IS_HANDLE(elem)) handle_container_err();
    if (IS_CHAN(elem)) chan_container_err();
    for (int i = 0; i < g_narrtypes; i++)
        if (g_arrtypes[i].elem == elem) return T_ARRC_BASE + i;
    if (g_narrtypes >= T_OPT_BASE - T_ARRC_BASE) { fprintf(stderr, "tychoc: too many array types\n"); exit(1); }
    TBL_ENSURE(g_arrtypes, g_narrtypes, g_arrtypes_cap);
    g_arrtypes[g_narrtypes].elem = elem;
    return T_ARRC_BASE + g_narrtypes++;
}
static int is_array(Type t) {
    return t == T_ARRAY_INT || t == T_ARRAY_STRING || t == T_ARRAY_FLOAT || IS_ARRC(t);
}
static Type arr_elem(Type arr) {
    if (arr == T_ARRAY_STRING) return T_STRING;
    if (arr == T_ARRAY_FLOAT)  return T_FLOAT;
    if (IS_ARRC(arr))          return g_arrtypes[ARRC_ID(arr)].elem;
    return T_INT;   /* T_ARRAY_INT */
}

/* Generics: a `$T` type parameter is interned by name. These types appear only
 * inside a generic function template; at each call the parameter is bound to a
 * concrete type and the instance is resolved/emitted with the binding, so a
 * T_TYPARAM never reaches codegen. `g_cur_typarams` is the in-scope set while
 * parsing one function's signature + body (Stage 1: functions only). */
typedef struct { char *name; } TyParam;
static TyParam *g_typarams;
static int g_ntyparams = 0, g_typarams_cap = 0;
#define IS_TYPARAM(t) ((t) >= T_TYPARAM_BASE)
static Type typaram_of(char *name) {
    for (int i = 0; i < g_ntyparams; i++)
        if (!strcmp(g_typarams[i].name, name)) return T_TYPARAM_BASE + i;
    TBL_ENSURE(g_typarams, g_ntyparams, g_typarams_cap);
    g_typarams[g_ntyparams].name = name;
    return T_TYPARAM_BASE + g_ntyparams++;
}
static char *typaram_name(Type t) { return g_typarams[(int)(t - T_TYPARAM_BASE)].name; }
static char *g_cur_typarams[16];
static int   g_ncur_typarams = 0;

/* Option(T) — a tagged optional (Some(value) or None). Interned like composite
 * arrays; one monomorphic TychoOpt<id> { char has; T val; } is generated per
 * inner type used. Ids sit above the array range (T_OPT_BASE, defined above). */
typedef struct { Type inner; } OptType;
static OptType *g_opttypes;
static int g_nopttypes = 0, g_opttypes_cap = 0;
#define IS_OPT(t)  ((t) >= T_OPT_BASE && (t) < T_RES_BASE)   /* Results sit above */
#define OPT_ID(t)  ((int)((t) - T_OPT_BASE))
static Type opt_of(Type inner) {                 /* find-or-create Option(inner) */
    if (IS_TASK(inner)) task_container_err();
    if (IS_HANDLE(inner)) handle_container_err();
    if (IS_CHAN(inner)) chan_container_err();
    for (int i = 0; i < g_nopttypes; i++)
        if (g_opttypes[i].inner == inner) return T_OPT_BASE + i;
    if (g_nopttypes >= T_RES_BASE - T_OPT_BASE) { fprintf(stderr, "tychoc: too many option types\n"); exit(1); }
    TBL_ENSURE(g_opttypes, g_nopttypes, g_opttypes_cap);
    g_opttypes[g_nopttypes].inner = inner;
    return T_OPT_BASE + g_nopttypes++;
}
static Type opt_inner(Type t) { return g_opttypes[OPT_ID(t)].inner; }

/* Result(T, E) — a tagged success-or-failure (Ok(value) or Err(error)). The
 * no-exceptions error story: a function returns Result(T, E) and the caller
 * matches Ok/Err. Interned like Option, but over TWO inner types; one
 * monomorphic TychoRes<id> { char ok; T okv; E errv; } is generated per (T,E)
 * pair used. Ids sit in [T_RES_BASE, T_ENUM_BASE). */
typedef struct { Type ok; Type err; } ResType;
static ResType *g_restypes;
static int g_nrestypes = 0, g_restypes_cap = 0;
#define IS_RES(t)  ((t) >= T_RES_BASE && (t) < T_ENUM_BASE)
#define RES_ID(t)  ((int)((t) - T_RES_BASE))
static Type res_of(Type ok, Type err) {          /* find-or-create Result(ok, err) */
    if (IS_TASK(ok) || IS_TASK(err)) task_container_err();
    if (IS_HANDLE(ok) || IS_HANDLE(err)) handle_container_err();
    if (IS_CHAN(ok) || IS_CHAN(err)) chan_container_err();
    for (int i = 0; i < g_nrestypes; i++)
        if (g_restypes[i].ok == ok && g_restypes[i].err == err) return T_RES_BASE + i;
    if (g_nrestypes >= T_ENUM_BASE - T_RES_BASE) { fprintf(stderr, "tychoc: too many result types\n"); exit(1); }
    TBL_ENSURE(g_restypes, g_nrestypes, g_restypes_cap);
    g_restypes[g_nrestypes].ok = ok; g_restypes[g_nrestypes].err = err;
    return T_RES_BASE + g_nrestypes++;
}
static Type res_ok(Type t)  { return g_restypes[RES_ID(t)].ok; }
static Type res_err(Type t) { return g_restypes[RES_ID(t)].err; }

/* User sum types (enums): one or more named variants, each with a payload tuple
 * of 0+ types. A value is a small descriptor { int tag; void *payload } — the
 * payload (the active variant's fields) is arena-allocated, so even a recursive
 * enum (an AST: Add(Expr, Expr)) is finite, the same way arrays/strings are.
 * Variant names are globally unique, so a constructor or match arm names the
 * variant directly with no qualification. */
typedef struct { char *name; Type payload[8]; int npayload; } Variant;
typedef struct { char *name; Variant *variants; int nvariants; int variants_cap; int line;
                 int generic; Type typarams[8]; int ntyparams;
                 int from_tmpl; Type from_args[8]; int nfrom_args; } EnumDef;   /* generics: `enum Tree($T)` template; instances substitute $T in variant payloads. from_tmpl>=0 records the template+args this instance came from (for matching a recursive self-reference). */
static EnumDef *g_enums;
static int g_nenums = 0, g_enums_cap = 0;
#define IS_ENUM(t)    ((t) >= T_ENUM_BASE && (t) < T_TUP_BASE)
#define ENUM_ID(t)    ((int)((t) - T_ENUM_BASE))
#define ENUM_TYPE(id) (T_ENUM_BASE + (id))
/* every variant nullary: usable as a map key (the tag IS the value; the cells
 * are per-variant immortal singletons). A payload enum is rejected as a key —
 * equal tags would not mean equal values. */
static int enum_fieldless(Type t) {
    if (!IS_ENUM(t)) return 0;
    EnumDef *ed = &g_enums[ENUM_ID(t)];
    for (int v = 0; v < ed->nvariants; v++)
        if (ed->variants[v].npayload != 0) return 0;
    return 1;
}
static int enum_find(const char *name) {
    for (int i = 0; i < g_nenums; i++)
        if (!strcmp(g_enums[i].name, name)) return i;
    return -1;
}
/* find a variant by its (globally unique) name: returns its enum id, and writes
 * the variant index through *vi. -1 if not a known variant. */
static int variant_find(const char *vname, int *vi) {
    for (int e = 0; e < g_nenums; e++)
        for (int v = 0; v < g_enums[e].nvariants; v++)
            if (!strcmp(g_enums[e].variants[v].name, vname)) { if (vi) *vi = v; return e; }
    return -1;
}

/* Tuples (T1, ..., Tn), n >= 2 — first-class anonymous product values, used for
 * multiple return values (`return a, b` builds one) but storable, passable, and
 * indexable (`t.0`) like any value. Interned like Option/Result; one monomorphic
 * TychoTup<id> { T0 _0; ...; Tn-1 _n-1; } is generated per distinct element-type
 * list. Ids sit at [T_TUP_BASE, ...). Deep-copied by value field-wise. */
typedef struct { Type elems[8]; int n; } TupType;
static TupType *g_tuptypes;
static int g_ntuptypes = 0, g_tuptypes_cap = 0;
#define IS_TUP(t)  ((t) >= T_TUP_BASE && (t) < T_NT_BASE)
#define TUP_ID(t)  ((int)((t) - T_TUP_BASE))
static Type tup_of(Type *elems, int n) {         /* find-or-create (elems...) */
    for (int i = 0; i < n; i++) {
        if (IS_TASK(elems[i])) task_container_err();
        if (IS_HANDLE(elems[i])) handle_container_err();
        if (IS_CHAN(elems[i])) chan_container_err();
    }
    for (int i = 0; i < g_ntuptypes; i++)
        if (g_tuptypes[i].n == n) {
            int same = 1;
            for (int j = 0; j < n; j++) if (g_tuptypes[i].elems[j] != elems[j]) { same = 0; break; }
            if (same) return T_TUP_BASE + i;
        }
    if (g_ntuptypes >= T_NT_BASE - T_TUP_BASE) { fprintf(stderr, "tychoc: too many tuple types\n"); exit(1); }
    TBL_ENSURE(g_tuptypes, g_ntuptypes, g_tuptypes_cap);
    g_tuptypes[g_ntuptypes].n = n;
    for (int j = 0; j < n; j++) g_tuptypes[g_ntuptypes].elems[j] = elems[j];
    return T_TUP_BASE + g_ntuptypes++;
}
static int  tup_n(Type t)         { return g_tuptypes[TUP_ID(t)].n; }
static Type tup_elem(Type t, int i) { return g_tuptypes[TUP_ID(t)].elems[i]; }

/* Distinct newtypes: `type Meters = float` declares a named type that is
 * type-incompatible with its underlying type and with every other newtype, but
 * has the SAME C representation (zero-cost). Underlying is int or float for now;
 * a newtype value supports its base's arithmetic/ordering/str ONLY between two
 * values of the SAME newtype, so units can't be mixed. Construct with Meters(x),
 * unwrap with to_int/to_float. Named like structs; registered at parse time. */
typedef struct { char *name; Type under; } NewtypeDef;
static NewtypeDef *g_newtypes;
static int g_nnewtypes = 0, g_newtypes_cap = 0;
#define T_SOA_BASE  28672  /* struct-of-arrays types `soa [Struct]`, above newtypes */
#define IS_NEWTYPE(t)  ((t) >= T_NT_BASE && (t) < T_SOA_BASE)
#define NT_ID(t)       ((int)((t) - T_NT_BASE))
#define NT_TYPE(id)    (T_NT_BASE + (id))
static int newtype_find(const char *name) {
    for (int i = 0; i < g_nnewtypes; i++)
        if (!strcmp(g_newtypes[i].name, name)) return i;
    return -1;
}
static Type nt_under(Type t) { return g_newtypes[NT_ID(t)].under; }
/* the underlying type seen through any newtype (else the type itself) */
static Type base_of(Type t) { return IS_NEWTYPE(t) ? nt_under(t) : t; }

/* SOA arrays: `soa [Struct]` is stored struct-of-arrays — one growable arena
 * buffer per struct field plus a shared len/cap — instead of one array of
 * records. Cache-friendly when a loop touches one field across all elements.
 * Interned per element struct type (a value is by-value, like the AoS arrays). */
typedef struct { Type st; } SoaType;            /* st = the element struct type */
static SoaType *g_soatypes;
static int g_nsoatypes = 0, g_soatypes_cap = 0;
#define IS_SOA(t)   ((t) >= T_SOA_BASE && (t) < T_MAPC_BASE)   /* composite maps sit above */
#define SOA_ID(t)   ((int)((t) - T_SOA_BASE))
static Type soa_of(Type st) {                   /* find-or-create soa [st] */
    for (int i = 0; i < g_nsoatypes; i++)
        if (g_soatypes[i].st == st) return T_SOA_BASE + i;
    if (g_nsoatypes >= T_MAPC_BASE - T_SOA_BASE) { fprintf(stderr, "tychoc: too many soa types\n"); exit(1); }
    TBL_ENSURE(g_soatypes, g_nsoatypes, g_soatypes_cap);
    g_soatypes[g_nsoatypes].st = st;
    return T_SOA_BASE + g_nsoatypes++;
}
static Type soa_struct(Type t) { return g_soatypes[SOA_ID(t)].st; }

/* ------------------------------------------- "did you mean ...?" */

/* Bounded Levenshtein distance for typo suggestions in diagnostics.
 * Names longer than 63 bytes never match (cheap upper bound). */
static int edit_dist(const char *a, const char *b) {
    int la = (int)strlen(a), lb = (int)strlen(b);
    if (la > 63 || lb > 63) return 99;
    int row[64];
    for (int j = 0; j <= lb; j++) row[j] = j;
    for (int i = 1; i <= la; i++) {
        int prev = row[0];
        row[0] = i;
        for (int j = 1; j <= lb; j++) {
            int tmp = row[j];
            int d = prev + (a[i - 1] == b[j - 1] ? 0 : 1);
            if (row[j] + 1 < d) d = row[j] + 1;
            if (row[j - 1] + 1 < d) d = row[j - 1] + 1;
            row[j] = d;
            prev = tmp;
        }
    }
    return row[lb];
}
/* consider `cand` as a suggestion for the unknown `name`; keep the closest */
static void dym(const char *name, const char *cand, const char **best, int *bestd) {
    if (!cand || !cand[0] || !strcmp(cand, name)) return;
    int d = edit_dist(name, cand);
    if (d < *bestd) { *bestd = d; *best = cand; }
}
/* only offer a suggestion close enough to be a plausible typo */
static const char *dym_pick(const char *name, const char *best, int bestd) {
    return bestd <= (strlen(name) <= 4 ? 1 : 2) ? best : NULL;
}
static const char *suggest_type(const char *name) {
    const char *best = NULL; int bestd = 99;
    for (int i = 0; i < g_nstructs; i++)  dym(name, g_structs[i].name, &best, &bestd);
    for (int i = 0; i < g_nenums; i++)    dym(name, g_enums[i].name, &best, &bestd);
    for (int i = 0; i < g_nnewtypes; i++) dym(name, g_newtypes[i].name, &best, &bestd);
    static const char *const kw[] = { "int", "float", "bool", "string" };
    for (int i = 0; i < (int)(sizeof kw / sizeof *kw); i++) dym(name, kw[i], &best, &bestd);
    return dym_pick(name, best, bestd);
}

/* "geom__" when t names a package-mangled user type (geom__Circle), else NULL.
 * Lets UFCS resolve a method defined in the receiver type's own package. */
static const char *type_pkg_prefix(Type t) {
    const char *nm = NULL;
    if (IS_STRUCT(t))       nm = g_structs[STRUCT_ID(t)].name;
    else if (IS_ENUM(t))    nm = g_enums[ENUM_ID(t)].name;
    else if (IS_NEWTYPE(t)) nm = g_newtypes[NT_ID(t)].name;
    if (!nm) return NULL;
    const char *us = strstr(nm, "__");
    if (!us) return NULL;
    return sfmt("%.*s", (int)(us - nm) + 2, nm);
}

/* Composite maps [K: V] with an arbitrary value type (string/struct/array/...),
 * interned like composite arrays: one monomorphic TychoMapC<id> generated per
 * distinct (key, value) pair used. The four hand-written int/float-valued maps
 * (T_MAP_S?/I?) keep their dedicated runtime; everything else is a MAPC. */
typedef struct { Type key; Type val; } MapType;
static MapType *g_maptypes;
static int g_nmaptypes = 0, g_maptypes_cap = 0;
#define IS_MAPC(t)  ((t) >= T_MAPC_BASE && (t) < T_FUNC_BASE)
#define MAPC_ID(t)  ((int)((t) - T_MAPC_BASE))
static Type mapc_of(Type k, Type v) {            /* find-or-create [k: v] */
    for (int i = 0; i < g_nmaptypes; i++)
        if (g_maptypes[i].key == k && g_maptypes[i].val == v) return T_MAPC_BASE + i;
    if (g_nmaptypes >= 16384) { fprintf(stderr, "tychoc: too many map types\n"); exit(1); }   /* T_FUNC_BASE - T_MAPC_BASE (defined below) */
    TBL_ENSURE(g_maptypes, g_nmaptypes, g_maptypes_cap);
    if (IS_TASK(v)) task_container_err();
    if (IS_HANDLE(v)) handle_container_err();
    if (IS_CHAN(v)) chan_container_err();
    g_maptypes[g_nmaptypes].key = k; g_maptypes[g_nmaptypes].val = v;
    return T_MAPC_BASE + g_nmaptypes++;
}

/* First-class function values: `fn(P1,...,Pn) -> R`. A value is a C function
 * pointer to a top-level function (no capture, so no closure/arena machinery —
 * a code pointer is immortal and not heap). Interned by signature like tuples;
 * emitted as `typedef R (*FnC<id>)(Arena*, P1,...,Pn)` matching every tycho fn's
 * uniform C ABI (the hidden return arena is always the first parameter). */
#define T_FUNC_BASE 49152
typedef struct { Type params[8]; int n; Type ret; } FuncTy;
static FuncTy *g_functypes;
static int g_nfunctypes = 0, g_functypes_cap = 0;
#define IS_FUNC(t)  ((t) >= T_FUNC_BASE && (t) < T_TASK_BASE)
#define FUNC_ID(t)  ((int)((t) - T_FUNC_BASE))
static Type funcc_of(Type *params, int n, Type ret) {   /* find-or-create fn(params) -> ret */
    /* fn VALUES are storable (containers, fields) -- a task/channel handle in
     * a fn type could smuggle one past every lifetime guard. Fail closed. */
    if (IS_TASK(ret) || IS_CHAN(ret)) { fprintf(stderr, "tychoc: a function value cannot return a task or channel handle\n"); exit(1); }
    for (int i = 0; i < n; i++)
        if (IS_TASK(params[i]) || IS_CHAN(params[i])) { fprintf(stderr, "tychoc: a function value cannot take a task or channel handle\n"); exit(1); }
    for (int i = 0; i < g_nfunctypes; i++) {
        if (g_functypes[i].n != n || g_functypes[i].ret != ret) continue;
        int same = 1;
        for (int j = 0; j < n; j++) if (g_functypes[i].params[j] != params[j]) { same = 0; break; }
        if (same) return T_FUNC_BASE + i;
    }
    if (g_nfunctypes >= T_TASK_BASE - T_FUNC_BASE) { fprintf(stderr, "tychoc: too many function types\n"); exit(1); }
    TBL_ENSURE(g_functypes, g_nfunctypes, g_functypes_cap);
    g_functypes[g_nfunctypes].n = n; g_functypes[g_nfunctypes].ret = ret;
    for (int j = 0; j < n; j++) g_functypes[g_nfunctypes].params[j] = params[j];
    return T_FUNC_BASE + g_nfunctypes++;
}
static int  func_n(Type t)            { return g_functypes[FUNC_ID(t)].n; }
static Type func_ret(Type t)          { return g_functypes[FUNC_ID(t)].ret; }
static Type func_param(Type t, int i) { return g_functypes[FUNC_ID(t)].params[i]; }
/* top-level functions taken as a value: each gets a `<name>__clo` thunk so a plain
 * reference becomes the fat value {0, <name>__clo}. */
static const char **g_fnval;
static int g_nfnval = 0, g_fnval_cap = 0;
static void note_fnval(const char *name) {
    for (int i = 0; i < g_nfnval; i++) if (!strcmp(g_fnval[i], name)) return;
    TBL_ENSURE(g_fnval, g_nfnval, g_fnval_cap);
    g_fnval[g_nfnval++] = name;
}

/* String-keyed maps come in two value flavours: [string: int] (TychoMapSI) and
 * [string: float] (TychoMapSF). map_fn picks the runtime infix, map_val the
 * value type, map_of the map type for a value type. */
static int is_map(Type t) { return t == T_MAP_SI || t == T_MAP_SF || t == T_MAP_II || t == T_MAP_IF || IS_MAPC(t); }
static const char *map_fn(Type t) {
    return t == T_MAP_SF ? "sf" : t == T_MAP_II ? "ii" : t == T_MAP_IF ? "if" : "si";
}
/* runtime fn name for a map op, dispatching hardcoded (si/sf/ii/if) vs composite. */
static char *map_rt(Type t, const char *op) {
    return IS_MAPC(t) ? sfmt("tycho_mapc%d_%s", MAPC_ID(t), op)
                      : sfmt("tycho_map_%s_%s", map_fn(t), op);
}
static Type map_val(Type t) { return IS_MAPC(t) ? g_maptypes[MAPC_ID(t)].val : (t == T_MAP_SF || t == T_MAP_IF) ? T_FLOAT : T_INT; }
static Type map_key(Type t) { return IS_MAPC(t) ? g_maptypes[MAPC_ID(t)].key : (t == T_MAP_II || t == T_MAP_IF) ? T_INT : T_STRING; }
/* does this map key ride the int-key (occupancy/long) storage scheme? */
static int mapkey_intrep(Type k) { return base_of(k) == T_INT || enum_fieldless(k); }
/* composite map key (Stage 1: a struct, possibly through a newtype). Rides the
 * occupancy scheme like int keys, but stores the struct by value and hashes/compares
 * it deeply (generated tycho_hash_S_ and tycho_eq_S_ functions). */
static int mapkey_composite(Type k) { Type b = base_of(k); return IS_STRUCT(b) || IS_TUP(b) || is_array(b); }
/* a type usable as (a field of) a composite key: every leaf must have a stable deep
 * hash. Scalars + bytes + (recursively) a struct/tuple/array of those qualify;
 * maps/enums/functions/handles do not yet (a later stage). */
static int key_hashable(Type t) {
    t = base_of(t);
    if (t == T_INT || t == T_FLOAT || t == T_BOOL || t == T_CHAR || t == T_STRING || t == T_BYTES) return 1;
    if (IS_STRUCT(t)) {
        StructDef *sd = &g_structs[STRUCT_ID(t)];
        for (int i = 0; i < sd->nfields; i++) if (!key_hashable(sd->fields[i].type)) return 0;
        return 1;
    }
    if (IS_TUP(t)) {
        for (int i = 0; i < tup_n(t); i++) if (!key_hashable(tup_elem(t, i))) return 0;
        return 1;
    }
    if (is_array(t)) return key_hashable(arr_elem(t));   /* an array key hashes element-wise (order-sensitive) */
    return 0;
}
/* is composite type `want` (struct/tuple/array) reachable inside key type kt -- the key
 * itself, or a struct field / tuple element / array element of it, recursively? */
static int struct_in_key(Type want, Type kt) {
    kt = base_of(kt);
    if (kt == want) return 1;
    if (IS_STRUCT(kt)) {
        StructDef *sd = &g_structs[STRUCT_ID(kt)];
        for (int j = 0; j < sd->nfields; j++)
            if (struct_in_key(want, sd->fields[j].type)) return 1;
    }
    if (IS_TUP(kt)) {
        for (int j = 0; j < tup_n(kt); j++)
            if (struct_in_key(want, tup_elem(kt, j))) return 1;
    }
    if (is_array(kt)) return struct_in_key(want, arr_elem(kt));
    return 0;
}
/* does any composite-keyed map use composite type `st` (struct or tuple) as its key or
 * a nested key field/element? Only such types get a tycho_hash_* emitted -- the hash
 * calls tycho_ik_hash / tycho_si_hash, only emitted when the program uses maps. */
static int struct_keyused(Type st) {
    for (int i = 0; i < g_nmaptypes; i++)
        if (mapkey_composite(g_maptypes[i].key) && struct_in_key(st, g_maptypes[i].key)) return 1;
    return 0;
}
/* a map key expression as the runtime stores it: a fieldless-enum key passes its TAG */
static char *key_rt(Type mt, char *kexpr) {
    return IS_ENUM(map_key(mt)) ? sfmt("((%s)->tag)", kexpr) : kexpr;
}
/* the map type for a (key, value) pair. Only string and int keys (directly or
 * through a newtype), int and float values exist; an unsupported pair returns
 * T_VOID (the caller rejects it). */
static Type arr_of(Type elem);   /* defined below; used to intern a newtype key's [K] */
static Type map_of(Type k, Type v) {
    if (k == T_STRING && (v == T_INT || v == T_FLOAT)) return v == T_FLOAT ? T_MAP_SF : T_MAP_SI;
    if (k == T_INT    && (v == T_INT || v == T_FLOAT)) return v == T_FLOAT ? T_MAP_IF : T_MAP_II;
    if (k == T_STRING) return mapc_of(T_STRING, v);   /* [string: V] composite, any value type */
    if (k == T_INT)    return mapc_of(T_INT, v);      /* [int: V] composite (occupancy-array scheme) */
    if (IS_NEWTYPE(k) && (nt_under(k) == T_INT || nt_under(k) == T_STRING)) {
        /* newtype key (base int or string): ALWAYS a composite, so the map type
         * carries the declared key and map_set/get/has/del stay distinct (a raw
         * base value is rejected). Storage and hashing are the base's. */
        Type mt = mapc_of(k, v);
        arr_of(k);   /* intern [K] now: the emitted keys() helper returns it */
        return mt;
    }
    if (enum_fieldless(k)) {
        /* fieldless-enum key: stored and hashed as its TAG (a long), riding the
         * int-key occupancy scheme; keys() rebuilds [E] from the per-variant
         * singleton table (immortal, share-safe). */
        Type mt = mapc_of(k, v);
        arr_of(k);
        return mt;
    }
    if (mapkey_composite(k) && key_hashable(k)) {
        /* struct key (Stage 1): stored by value, deep-hashed/compared, occupancy scheme;
         * keys() returns [K]. mapc_of interns on (k,v); the runtime is emitted per pair. */
        Type mt = mapc_of(k, v);
        arr_of(k);
        return mt;
    }
    return T_VOID;
}

/* A "heap" type owns arena-allocated bytes outside its own value word(s):
 * string (char* into an arena), [int]/[string] (a buffer), or any struct
 * that (transitively) contains such a field. int/bool and pure structs are
 * not heap: copying the value word is a complete copy. This is what decides
 * whether a move (decl/assign/return/field-set/construction) must deep-copy
 * to keep the implicit-arena model sound. Structs are defined before use, so
 * a field's struct type is fully known here — no cycles, recursion ends. */
static int type_is_heap(Type t) {
    if (IS_NEWTYPE(t)) return type_is_heap(nt_under(t));   /* same rep as its base */
    if (IS_SOA(t)) return 1;                               /* holds heap field-array pointers */
    if (IS_FUNC(t)) return 1;   /* a closure carries an env that may be heap; copy_into re-homes it (a plain ref has env==0 -> no-op) */
    if (t == T_STRING || t == T_BYTES || is_map(t) || is_array(t)) return 1;   /* bytes shares string's heap buffer */
    if (IS_OPT(t)) return type_is_heap(opt_inner(t));   /* heap iff its value is */
    if (IS_RES(t)) return type_is_heap(res_ok(t)) || type_is_heap(res_err(t));
    if (IS_TUP(t)) {   /* heap iff any element is */
        for (int i = 0; i < tup_n(t); i++) if (type_is_heap(tup_elem(t, i))) return 1;
        return 0;
    }
    if (IS_ENUM(t)) {   /* heap iff any variant carries a payload (an arena ptr) */
        EnumDef *ed = &g_enums[ENUM_ID(t)];
        for (int i = 0; i < ed->nvariants; i++)
            if (ed->variants[i].npayload > 0) return 1;
        return 0;
    }
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
    if (IS_NEWTYPE(t)) return c_type(nt_under(t));   /* zero-cost: emitted as its base C type */
    if (IS_STRUCT(t)) return sfmt("S_%s ", g_structs[STRUCT_ID(t)].name);
    if (IS_ARRC(t))   return sfmt("TychoArrC%d ", ARRC_ID(t));
    if (IS_MAPC(t))   return sfmt("TychoMapC%d ", MAPC_ID(t));
    if (IS_OPT(t))    return sfmt("TychoOpt%d ", OPT_ID(t));
    if (IS_RES(t))    return sfmt("TychoRes%d ", RES_ID(t));
    if (IS_TUP(t))    return sfmt("TychoTup%d ", TUP_ID(t));
    if (IS_FUNC(t))   return sfmt("FnC%d ", FUNC_ID(t));   /* a function-pointer typedef */
    if (IS_TASK(t))   return "HTask *";   /* opaque runtime task handle (spawn/wait) */
    if (IS_CHAN(t))   return "HChan *";   /* shared bounded-queue handle (channels) */
    if (IS_HANDLE(t)) return "void *";    /* typed C handle (FFI R2): opaque void*, freed by its destructor at scope exit */
    if (IS_ENUM(t))   return sfmt("E_%s *", g_enums[ENUM_ID(t)].name);   /* a value is a pointer to a tagged cell */
    if (IS_SOA(t))    return sfmt("Soa%d ", SOA_ID(t));
    switch (t) {
        case T_INT:          return "long ";
        case T_CHAR:         return "long ";
        case T_U32:          return "unsigned int ";        /* wraps at 2^32 natively */
        case T_U64:          return "unsigned long long ";  /* wraps at 2^64 natively */
        case T_F32:          return "float ";
        case T_FLOAT:        return "double ";
        case T_BOOL:         return "int ";
        case T_PTR:          return "void *";
        case T_STRING:       return "char *";
        case T_BYTES:        return "char *";   /* same length-headered buffer as string */
        case T_ARRAY_INT:    return "TychoArrInt ";
        case T_ARRAY_FLOAT:  return "TychoArrFloat ";
        case T_ARRAY_STRING: return "TychoArrStr ";
        case T_MAP_SI:       return "TychoMapSI ";
        case T_MAP_SF:       return "TychoMapSF ";
        case T_MAP_II:       return "TychoMapII ";
        case T_MAP_IF:       return "TychoMapIF ";
        default:             return "void ";
    }
}
static const char *type_name(Type t) {
    if (IS_NEWTYPE(t)) return g_newtypes[NT_ID(t)].name;
    if (IS_TASK(t))    return sfmt("Task(%s)", type_name(task_inner(t)));
    if (IS_CHAN(t))    return sfmt("Channel(%s)", type_name(chan_inner(t)));
    if (IS_HANDLE(t))  return g_handles[HANDLE_ID(t)].name;
    if (IS_STRUCT(t)) return g_structs[STRUCT_ID(t)].name;
    if (IS_ARRC(t))   return sfmt("[%s]", type_name(arr_elem(t)));
    if (IS_MAPC(t))   return sfmt("[%s: %s]", type_name(map_key(t)), type_name(map_val(t)));
    if (IS_OPT(t))    return sfmt("Option(%s)", type_name(opt_inner(t)));
    if (IS_RES(t))    return sfmt("Result(%s, %s)", type_name(res_ok(t)), type_name(res_err(t)));
    if (IS_TUP(t)) {
        char *s = sfmt("(%s", type_name(tup_elem(t, 0)));
        for (int i = 1; i < tup_n(t); i++) s = sfmt("%s, %s", s, type_name(tup_elem(t, i)));
        return sfmt("%s)", s);
    }
    if (IS_FUNC(t)) {
        char *s = sfmt("fn(");
        for (int i = 0; i < func_n(t); i++) s = sfmt("%s%s%s", s, i ? ", " : "", type_name(func_param(t, i)));
        s = sfmt("%s)", s);
        if (func_ret(t) != T_VOID) s = sfmt("%s -> %s", s, type_name(func_ret(t)));
        return s;
    }
    if (IS_ENUM(t))   return g_enums[ENUM_ID(t)].name;
    if (IS_SOA(t))    return sfmt("soa [%s]", type_name(soa_struct(t)));
    switch (t) {
        case T_NONE:         return "None";
        case T_OK_PARTIAL:   return "Ok(...)";
        case T_ERR_PARTIAL:  return "Err(...)";
        case T_INT:          return "int";
        case T_U32:          return "u32";
        case T_U64:          return "u64";
        case T_F32:          return "f32";
        case T_FLOAT:        return "float";
        case T_BOOL:         return "bool";
        case T_PTR:          return "ptr";
        case T_STRING:       return "string";
        case T_BYTES:        return "bytes";
        case T_ARRAY_INT:    return "[int]";
        case T_ARRAY_FLOAT:  return "[float]";
        case T_ARRAY_STRING: return "[string]";
        case T_MAP_SI:       return "[string: int]";
        case T_MAP_SF:       return "[string: float]";
        case T_MAP_II:       return "[int: int]";
        case T_MAP_IF:       return "[int: float]";
        default:             return "void";
    }
}

/* An array type's runtime-function infix: tycho_arr_<fn>_push etc. The fixed
 * arrays use "int"/"str"/"float" (hand-written); a composite array uses
 * "C<id>" (generated). (arr_elem and is_array are defined above, near the
 * interned table, since c_type/type_name need them.) */
static const char *arr_fn(Type arr) {
    if (arr == T_ARRAY_STRING) return "str";
    if (arr == T_ARRAY_FLOAT)  return "float";
    if (IS_ARRC(arr))          return sfmt("C%d", ARRC_ID(arr));
    return "int";   /* T_ARRAY_INT */
}
/* the array type whose element is `elem`: a fixed one for int/float/string,
 * else an interned composite (struct or nested-array element). */
static Type arr_of(Type elem) {
    if (elem == T_STRING) return T_ARRAY_STRING;
    if (elem == T_FLOAT)  return T_ARRAY_FLOAT;
    if (elem == T_INT)    return T_ARRAY_INT;
    return arrc_of(elem);   /* struct or array element */
}

typedef enum { E_INT, E_FLOAT, E_STR, E_CHAR, E_BOOL, E_IDENT, E_BINOP, E_CALL, E_ARRLIT, E_INDEX,
               E_STRUCTLIT, E_FIELD, E_ADDR, E_SOME, E_NONE, E_OK, E_ERR,
               E_ORRETURN, /* `e or_return`: unwrap Ok, else propagate Err from the enclosing fn */
               E_TUPLE,    /* (e1, ..., en): a tuple literal (also what `return a, b` builds) */
               E_TUPIDX,   /* t.0 / t.1: a tuple element by integer index (in ival) */
               E_SLICE,    /* xs[a:b]: a sub-range view (lhs=array, rhs=lo or NULL, args[0]=hi or NULL) */
               E_LAMBDA,   /* fn(p)->r: e closure literal; ival indexes g_laminfo */
               E_NULL,     /* `null`: the opaque ptr literal (void*)0 (FFI) */
               E_SPAWN     /* spawn f(args): run the call on a new thread; lhs = the E_CALL, ival = spawn-site id */ } ExprKind;

typedef struct Expr Expr;
struct Expr {
    ExprKind kind;
    Type     type;     /* filled in by resolver */
    int      line;
    long     ival;     /* E_INT / E_BOOL */
    double   fval;     /* E_FLOAT */
    char    *sval;     /* E_STR contents / E_IDENT name / E_CALL callee */
    TokKind  op;       /* E_BINOP */
    Expr    *lhs, *rhs;
    Expr   **args; int nargs;   /* E_CALL */
    const char *pkg;   /* E_CALL: prefix of the package this call appears in ("" = main); used to resolve a package-local name */
    const char *qual;  /* E_CALL: explicit qualifier of `pkg.name(...)` (the source ident, e.g. "geom"); NULL if unqualified */
    Type   *typeargs; int ntypeargs;   /* E_CALL: explicit call-site type args `name$(int, ...)`; 0 = none (inferred) */
};

typedef enum { S_DECL, S_ASSIGN, S_RETURN, S_IF, S_WHILE, S_FORRANGE,
               S_INDEXSET, S_FIELDSET, S_EXPR, S_MATCH, S_MDECL, S_MASSIGN,
               S_BREAK, S_CONTINUE,
               S_CONST, /* `const NAME = <literal>` local: folded at use, no runtime storage */
               S_SELECT /* select over channel recv arms + default/closed (CC-5) */ } StmtKind;

typedef struct Stmt Stmt;
/* one arm of a `match`: a variant name (Some/None or an enum variant), the
 * names it binds from the payload, and its block. */
typedef struct { char *variant; char *binds[8]; int nbinds; Stmt **body; int nbody; int line; } MatchArm;
struct Stmt {
    StmtKind kind;
    int      line;
    char    *name;         /* S_DECL / S_ASSIGN target, or S_FORRANGE loop var */
    Type     decl_type;    /* S_DECL resolved type */
    int      typed_decl;   /* S_DECL: had an explicit type annotation */
    Type     annot;        /* explicit annotation when typed_decl */
    Expr    *expr;         /* value / condition / return / S_INDEXSET rhs / match scrutinee */
    Expr    *target;       /* S_INDEXSET lvalue (an E_INDEX) */
    Expr    *r_start, *r_stop, *r_step;  /* S_FORRANGE; r_step NULL means 1 */
    char    *names[8]; int nnames;       /* S_MDECL targets: `a, b := f()` */
    Type     mtypes[8];                  /* S_MDECL resolved element types */
    Stmt   **body; int nbody;
    Stmt   **els;  int nels;
    MatchArm *arms; int narms;           /* S_MATCH / S_SELECT (variant = "recv"/"default"/"closed") */
    Expr   **sel_ch;                     /* S_SELECT: per-arm channel expr (NULL for default/closed) */
    Stmt    *ctrl;                       /* value if/match: `x := if.../match...` — the S_IF/S_MATCH whose single-expr branch tails feed this decl (only set on S_DECL; other tail positions desugar at parse time) */
    int      parallel;                   /* S_FORRANGE: `parallel for` (CC-3) */
    int      foreach;                    /* S_FORRANGE parallel: deferred `parallel for x in EXPR` (name=var, r_start=src ident, body=raw); resolve_parfor type-branches array vs channel */
    int      par_id;                     /* S_FORRANGE parallel: index into g_parfor */
};

typedef struct { char *name; Type type; int is_inout; int is_sink; const char *ffi_ct; } Param;   /* ffi_ct: FFI-boundary sized C type ("unsigned int " etc.) for a u8/u16/.../i64 extern param — NULL = use c_type(type) (which is int) */

typedef struct {
    char   *name;
    Param  *params; int nparams;
    Type    ret;
    int     has_ret;       /* explicit -> type present */
    Stmt  **body; int nbody;
    int     line;
    int     is_extern;     /* FFI: `extern fn` — bodyless, calls a C symbol directly (no arena, name unmangled) */
    const char *lib;       /* FFI: `extern "Lib" fn` — link with -lLib; NULL for bare extern */
    const char *ret_ffi_ct;/* FFI-boundary sized C return type ("unsigned int " etc.) for a u8/.../i64 extern return; NULL = use c_type(ret) */
    int     generic;       /* generics: a `$T` template — not sig-registered/emitted directly; instantiated per call */
    char   *con_pred[8];   /* generics: `where` predicate name (numeric/comparable/has_str); NULL for a type-set constraint */
    Type    con_tp[8];     /* the type parameter each constraint constrains (a T_TYPARAM) */
    Type    con_set[8][16];/* type-set constraint `T: a | b | ...` -- the allowed types (con_nset[c] of them) */
    int     con_nset[8];   /* type-set member count; 0 => it's a predicate (use con_pred) */
    int     ncon;
    Type    typarams[16];  /* generics: the template's $-params in declaration (first-appearance) order */
    int     ntyparams;     /* for mapping explicit call-site type args `f$(int, ...)` by position */
} Proc;

typedef struct { Proc **v; int n, cap; } ProcVec;

/* A lambda literal (E_LAMBDA.ival indexes here). `proc` is the lifted top-level
 * function: its params are [captures...][lambda params...] (so its body codegen is
 * ordinary). `ncap` captures lead; the rest are the lambda's own params. */
typedef struct { Proc *proc; int ncap; Type ftype; } LamInfo;
static LamInfo *g_laminfo;
static int g_laminfo_cap = 0;
static int g_nlaminfo = 0;
static ProcVec g_lambda_procs;   /* lifted lambda procs, emitted after the user procs */

/* --------------------------------------------------------------- parser */

#define TYCHO_MAX_PARSE_DEPTH 256
typedef struct { Tok *t; int p; int depth; } Parser;

static Tok *cur(Parser *ps)  { return &ps->t[ps->p]; }
static Tok *peek(Parser *ps, int k) { return &ps->t[ps->p + k]; }
static int  at(Parser *ps, TokKind k) { return cur(ps)->kind == k; }

static Tok *eat(Parser *ps, TokKind k, const char *what) {
    if (!at(ps, k)) { g_err_col = cur(ps)->col; die_at(cur(ps)->line, "expected %s", what); }
    return &ps->t[ps->p++];
}
static int accept(Parser *ps, TokKind k) {
    if (at(ps, k)) { ps->p++; return 1; }
    return 0;
}

/* While parsing an imported package, every top-level def name and every
 * user-type reference is prefixed with "<pkg>__" so distinct packages never
 * collide in the one flat namespace. "" for the main/entry package and for
 * single-file programs, which keeps their output byte-identical. */
static const char *g_cur_pkg_prefix = "";
static char *pkg_mangle(const char *n) {   /* identity when the prefix is empty (main) */
    return g_cur_pkg_prefix[0] ? sfmt("%s%s", g_cur_pkg_prefix, n) : (char *)n;
}
static char *pkg_prefix_for(const char *qualifier);   /* defined after the import table */
static void check_pkg_private(const char *qualifier, const char *name, int line);   /* B3: reject cross-package access to a leading-underscore name */

static char *type_mangle_ident(Type t);   /* fwd: defined with the Stage-1 generics helpers */

/* Generics (structs): substitute type parameters in a type — stamps out a struct
 * instance's concrete field types. binds is indexed by typaram id; T_VOID = unbound. */
static int struct_instantiate(int tmpl, Type *binds);   /* mutually recursive with subst_type (recursive generic types) */
static int enum_instantiate(int tmpl, Type *binds);
static Type subst_type(Type t, Type *binds) {
    if (IS_TYPARAM(t)) { Type b = binds[(int)(t - T_TYPARAM_BASE)]; return b == T_VOID ? t : b; }
    /* a bare *generic template* type is a deferred self/recursive reference (e.g. the
     * `LL($T)` inside `struct LL($T)`): concretize it now with the current bindings.
     * struct_instantiate dedups via struct_find BEFORE registering, so a type that
     * references itself terminates (the in-progress instance is found and reused). */
    if (IS_STRUCT(t) && g_structs[STRUCT_ID(t)].generic) return STRUCT_TYPE(struct_instantiate(STRUCT_ID(t), binds));
    if (IS_ENUM(t)   && g_enums[ENUM_ID(t)].generic)     return ENUM_TYPE(enum_instantiate(ENUM_ID(t), binds));
    if (is_array(t)) return arr_of(subst_type(arr_elem(t), binds));   /* arr_of canonicalizes [int]->T_ARRAY_INT */
    if (IS_OPT(t))  return opt_of(subst_type(opt_inner(t), binds));
    if (IS_RES(t))  return res_of(subst_type(g_restypes[RES_ID(t)].ok, binds), subst_type(g_restypes[RES_ID(t)].err, binds));
    if (is_map(t))  return map_of(subst_type(map_key(t), binds), subst_type(map_val(t), binds));   /* map_of canonicalizes [string:int]->T_MAP_SI */
    if (IS_FUNC(t)) {   /* fn(P...)->R: substitute each parameter + the return (higher-order generics) */
        Type ps[8]; int n = func_n(t);
        for (int i = 0; i < n; i++) ps[i] = subst_type(func_param(t, i), binds);
        return funcc_of(ps, n, subst_type(func_ret(t), binds));
    }
    return t;
}

/* Does a type (recursively) still mention a generic type parameter? Such a type
 * is transient (only in a template) and must never reach codegen. */
static int has_typaram(Type t) {
    if (IS_TYPARAM(t)) return 1;
    if (IS_STRUCT(t)) return g_structs[STRUCT_ID(t)].generic;   /* a bare generic template type is transient (a deferred self-reference) */
    if (IS_ENUM(t))   return g_enums[ENUM_ID(t)].generic;
    if (is_array(t)) return has_typaram(arr_elem(t));
    if (IS_OPT(t))  return has_typaram(opt_inner(t));
    if (IS_RES(t))  return has_typaram(g_restypes[RES_ID(t)].ok) || has_typaram(g_restypes[RES_ID(t)].err);
    if (is_map(t))  return has_typaram(map_key(t)) || has_typaram(map_val(t));
    if (IS_FUNC(t)) {
        if (has_typaram(func_ret(t))) return 1;
        for (int i = 0; i < func_n(t); i++) if (has_typaram(func_param(t, i))) return 1;
        return 0;
    }
    return 0;
}

/* Match a (possibly type-parameterized) field type against a concrete argument
 * type, binding type parameters; 0 on a conflict/mismatch. */
static int match_type(Type pat, Type concrete, Type *binds) {
    if (IS_TYPARAM(pat)) {
        int id = (int)(pat - T_TYPARAM_BASE);
        if (binds[id] != T_VOID && binds[id] != concrete) return 0;
        binds[id] = concrete; return 1;
    }
    if (is_array(pat) && is_array(concrete)) return match_type(arr_elem(pat), arr_elem(concrete), binds);
    if (IS_OPT(pat) && IS_OPT(concrete))   return match_type(opt_inner(pat), opt_inner(concrete), binds);
    if (IS_RES(pat) && IS_RES(concrete))   return match_type(g_restypes[RES_ID(pat)].ok, g_restypes[RES_ID(concrete)].ok, binds)
                                               && match_type(g_restypes[RES_ID(pat)].err, g_restypes[RES_ID(concrete)].err, binds);
    if (is_map(pat) && is_map(concrete))   return match_type(map_key(pat), map_key(concrete), binds)
                                               && match_type(map_val(pat), map_val(concrete), binds);
    /* a deferred generic self-reference (the bare template type) matched against a
     * concrete instance of that template: recover the parameter bindings from the
     * instance's provenance -- matching `Tree` against `Tree__int` binds T=int. */
    if (IS_STRUCT(pat) && g_structs[STRUCT_ID(pat)].generic
        && IS_STRUCT(concrete) && g_structs[STRUCT_ID(concrete)].from_tmpl == STRUCT_ID(pat)) {
        StructDef *in = &g_structs[STRUCT_ID(concrete)];
        for (int i = 0; i < in->nfrom_args; i++)
            if (!match_type(g_structs[STRUCT_ID(pat)].typarams[i], in->from_args[i], binds)) return 0;
        return 1;
    }
    if (IS_ENUM(pat) && g_enums[ENUM_ID(pat)].generic
        && IS_ENUM(concrete) && g_enums[ENUM_ID(concrete)].from_tmpl == ENUM_ID(pat)) {
        EnumDef *in = &g_enums[ENUM_ID(concrete)];
        for (int i = 0; i < in->nfrom_args; i++)
            if (!match_type(g_enums[ENUM_ID(pat)].typarams[i], in->from_args[i], binds)) return 0;
        return 1;
    }
    if (IS_FUNC(pat) && IS_FUNC(concrete) && func_n(pat) == func_n(concrete)) {   /* fn(P...)->R: bind $T from the param + return types (higher-order generics) */
        if (!match_type(func_ret(pat), func_ret(concrete), binds)) return 0;
        for (int i = 0; i < func_n(pat); i++)
            if (!match_type(func_param(pat, i), func_param(concrete, i), binds)) return 0;
        return 1;
    }
    return pat == concrete;
}

/* Find or stamp out the concrete struct instance of a generic template for the
 * given bindings: Box($T) + {T:int} -> a real `struct Box__int` with substituted
 * field types. The instance is an ordinary (non-generic) struct from here on. */
static int struct_instantiate(int tmpl, Type *binds) {
    char *nm = g_structs[tmpl].name;
    for (int i = 0; i < g_structs[tmpl].ntyparams; i++)
        nm = sfmt("%s__%s", nm, type_mangle_ident(binds[(int)(g_structs[tmpl].typarams[i] - T_TYPARAM_BASE)]));
    int ex = struct_find(nm);
    if (ex >= 0) return ex;
    if (g_nstructs >= T_ARRC_BASE - T_STRUCT_BASE) die_at(g_structs[tmpl].line, "too many structs");
    TBL_ENSURE(g_structs, g_nstructs, g_structs_cap);
    int id = g_nstructs++;
    { StructDef *s = &g_structs[id]; memset(s, 0, sizeof *s); s->name = nm; s->line = g_structs[tmpl].line;
      s->from_tmpl = tmpl; s->nfrom_args = g_structs[tmpl].ntyparams;   /* provenance: lets match_type recover $T from a recursive-self argument */
      for (int i = 0; i < g_structs[tmpl].ntyparams; i++)
          s->from_args[i] = binds[(int)(g_structs[tmpl].typarams[i] - T_TYPARAM_BASE)]; }
    /* Register the (empty) instance BEFORE substituting fields, so a recursive field
     * type (`tail: Option(LL($T))`) re-instantiates and finds THIS in-progress id.
     * subst_type may re-enter struct_instantiate and realloc g_structs, so never hold
     * a StructDef* across it: re-read the template field and re-fetch the instance each
     * iteration, and append the substituted type only after subst_type returns. */
    int nf = g_structs[tmpl].nfields;
    for (int f = 0; f < nf; f++) {
        char *fname = g_structs[tmpl].fields[f].name;
        Type   sty  = subst_type(g_structs[tmpl].fields[f].type, binds);
        StructDef *s = &g_structs[id];
        TBL_ENSURE(s->fields, s->nfields, s->fields_cap);
        s = &g_structs[id];   /* TBL_ENSURE grows s->fields (not g_structs), but re-fetch for safety */
        s->fields[s->nfields].name = fname;
        s->fields[s->nfields].type = sty;
        s->nfields++;
    }
    return id;
}

/* Find or stamp out the concrete enum instance of a generic template for the
 * given bindings: Tree($T) + {T:int} -> a real `enum Tree__int` whose variant
 * payloads have $T substituted. Variant names stay shared (lookups during a
 * `match` are keyed on the matched enum, not the global variant table), so the
 * instance is an ordinary (non-generic) enum from here on. */
static int enum_instantiate(int tmpl, Type *binds) {
    char *nm = g_enums[tmpl].name;
    for (int i = 0; i < g_enums[tmpl].ntyparams; i++)
        nm = sfmt("%s__%s", nm, type_mangle_ident(binds[(int)(g_enums[tmpl].typarams[i] - T_TYPARAM_BASE)]));
    int ex = enum_find(nm);
    if (ex >= 0) return ex;
    if (g_nenums >= T_TUP_BASE - T_ENUM_BASE) die_at(g_enums[tmpl].line, "too many enums");
    TBL_ENSURE(g_enums, g_nenums, g_enums_cap);
    int id = g_nenums++;
    { EnumDef *e = &g_enums[id]; memset(e, 0, sizeof *e); e->name = nm; e->line = g_enums[tmpl].line;
      e->from_tmpl = tmpl; e->nfrom_args = g_enums[tmpl].ntyparams;   /* provenance: lets match_type recover $T from a recursive-self argument */
      for (int i = 0; i < g_enums[tmpl].ntyparams; i++)
          e->from_args[i] = binds[(int)(g_enums[tmpl].typarams[i] - T_TYPARAM_BASE)]; }
    /* Register the (empty) instance BEFORE substituting payloads, so a recursive
     * payload (`Node(Tree($T))`) re-instantiates and finds THIS in-progress id.
     * subst_type may re-enter enum_instantiate and realloc g_enums, so buffer the
     * substituted payload locally and re-fetch the instance afterward; never hold an
     * EnumDef or Variant pointer across subst_type. */
    int nv = g_enums[tmpl].nvariants;
    for (int v = 0; v < nv; v++) {
        char *vname = g_enums[tmpl].variants[v].name;
        int   np    = g_enums[tmpl].variants[v].npayload;
        Type  pl[8];
        for (int f = 0; f < np; f++)
            pl[f] = subst_type(g_enums[tmpl].variants[v].payload[f], binds);   /* may realloc g_enums */
        EnumDef *e = &g_enums[id];
        TBL_ENSURE(e->variants, e->nvariants, e->variants_cap);
        e = &g_enums[id];
        Variant *dst = &e->variants[e->nvariants];
        dst->name = vname; dst->npayload = np;
        for (int f = 0; f < np; f++) dst->payload[f] = pl[f];
        e->nvariants++;
    }
    return id;
}

/* parse_type recurses on every `[T]` / `(T,...)` / `[K:V]` / `Option(T)` / `fn(...)`
 * nesting level, so a pathologically nested type annotation (`[[[...]]]int`,
 * `Option(Option(...))`) overflows the C stack. Guard it on the same depth budget
 * as expressions so it fails closed instead of SIGSEGV. */
static Type parse_type_inner(Parser *ps);
static Type parse_type(Parser *ps) {
    if (++ps->depth > TYCHO_MAX_PARSE_DEPTH) die_at(cur(ps)->line, "type nesting too deep");
    Type t = parse_type_inner(ps);
    ps->depth--;
    return t;
}
static Type parse_type_inner(Parser *ps) {
    Tok *t = cur(ps);
    if (t->kind == TK_DOLLAR) {          /* generics: `$T` introduces a type parameter into scope */
        ps->p++;
        Tok *nm = eat(ps, TK_IDENT, "a type-parameter name after '$'");
        int seen = 0;
        for (int i = 0; i < g_ncur_typarams; i++) if (!strcmp(g_cur_typarams[i], nm->text)) { seen = 1; break; }
        if (!seen) {
            if (g_ncur_typarams >= 16) die_at(nm->line, "too many type parameters (max 16)");
            g_cur_typarams[g_ncur_typarams++] = nm->text;
        }
        return typaram_of(nm->text);
    }
    if (t->kind == TK_IDENT && !strcmp(t->text, "soa")) {   /* soa [Struct] */
        ps->p++;
        eat(ps, TK_LBRACKET, "'[' after soa");
        Type el = parse_type(ps);
        eat(ps, TK_RBRACKET, "']'");
        if (!IS_STRUCT(el)) die_at(t->line, "soa requires a struct element type, e.g. soa [Point]");
        return soa_of(el);
    }
    if (t->kind == TK_FN) {              /* function type: fn(P1, ..., Pn) [-> R] */
        ps->p++;
        eat(ps, TK_LPAREN, "'(' after fn in a function type");
        Type params[8]; int n = 0;
        if (!at(ps, TK_RPAREN)) {
            params[n++] = parse_type(ps);
            while (accept(ps, TK_COMMA)) {
                if (n >= 8) die_at(t->line, "a function type has at most 8 parameters");
                params[n++] = parse_type(ps);
            }
        }
        eat(ps, TK_RPAREN, "')'");
        Type ret = T_VOID;
        if (accept(ps, TK_ARROW)) ret = parse_type(ps);   /* no arrow => void return */
        for (int i = 0; i < n; i++)
            if (params[i] == T_VOID) die_at(t->line, "a function-type parameter cannot be void");
        return funcc_of(params, n, ret);
    }
    if (t->kind == TK_LPAREN) {          /* tuple type (T1, ..., Tn), n >= 2 */
        ps->p++;
        Type elems[8]; int n = 0;
        elems[n++] = parse_type(ps);
        while (accept(ps, TK_COMMA)) {
            if (n >= 8) die_at(t->line, "a tuple has at most 8 elements");
            elems[n++] = parse_type(ps);
        }
        eat(ps, TK_RPAREN, "')'");
        if (n < 2) die_at(t->line, "a tuple type needs at least two elements");
        for (int i = 0; i < n; i++) {
            if (elems[i] == T_VOID) die_at(t->line, "a tuple element cannot be void");
        }
        return tup_of(elems, n);
    }
    if (t->kind == TK_LBRACKET) {        /* [int] / [string] / [string: int] */
        ps->p++;
        Type elem = parse_type(ps);
        if (at(ps, TK_COLON)) {          /* map type: [K: V] */
            ps->p++;
            Type val = parse_type(ps);
            eat(ps, TK_RBRACKET, "']'");
            if (has_typaram(elem) || has_typaram(val))   /* generics: a `[$K: $V]` pattern -- key/value validity is checked at instantiation */
                return mapc_of(elem, val);
            Type mt = map_of(elem, val);   /* map_of routes composite values to mapc_of; only a bad key is T_VOID */
            if (mt == T_VOID)
                die_at(t->line, "map keys must be string, int (directly or through a newtype), or a fieldless enum; int-keyed maps support only int/float values");
            return mt;
        }
        eat(ps, TK_RBRACKET, "']'");
        if (elem == T_VOID || elem == T_BOOL)
            die_at(t->line, "array elements must be int, float, string, a struct, or an array");
        return arr_of(elem);   /* fixed [int]/[float]/[string] or a composite */
    }
    if (t->kind == TK_IDENT && !strcmp(t->text, "Option")) {   /* Option(T) */
        ps->p++;
        eat(ps, TK_LPAREN, "'(' after Option");
        Type inner = parse_type(ps);
        eat(ps, TK_RPAREN, "')'");
        if (inner == T_VOID) die_at(t->line, "Option(void) is not a type");
        return opt_of(inner);
    }
    if (t->kind == TK_IDENT && !strcmp(t->text, "Channel")) {   /* Channel(T): a worker param's queue type (CC-4) */
        ps->p++;
        eat(ps, TK_LPAREN, "'(' after Channel");
        Type inner = parse_type(ps);
        eat(ps, TK_RPAREN, "')'");
        if (inner == T_VOID) die_at(t->line, "Channel(void) is not a type");
        return chan_of(inner);
    }
    if (t->kind == TK_IDENT && !strcmp(t->text, "Result")) {   /* Result(T, E) */
        ps->p++;
        eat(ps, TK_LPAREN, "'(' after Result");
        Type ok = parse_type(ps);
        eat(ps, TK_COMMA, "',' between Result's ok and error types");
        Type err = parse_type(ps);
        eat(ps, TK_RPAREN, "')'");
        if (ok == T_VOID || err == T_VOID) die_at(t->line, "Result's types cannot be void");
        return res_of(ok, err);
    }
    if (t->kind == TK_IDENT) {           /* a struct, enum, or newtype name */
        for (int i = 0; i < g_ncur_typarams; i++)   /* generics: a bare reference to an in-scope type parameter */
            if (!strcmp(g_cur_typarams[i], t->text)) { ps->p++; return typaram_of(t->text); }
        const char *nm;
        if (peek(ps, 1)->kind == TK_DOT && peek(ps, 2)->kind == TK_IDENT) {
            /* qualified type `pkg.Type` -> the imported package's mangled name */
            check_pkg_private(t->text, peek(ps, 2)->text, t->line);
            nm = sfmt("%s%s", pkg_prefix_for(t->text), peek(ps, 2)->text);
            ps->p += 2;                  /* skip qualifier + dot; the type-name ident is consumed on a hit below */
        } else {
            nm = pkg_mangle(t->text);    /* package-local: try the current package's prefixed name */
        }
        int sid = struct_find(nm);
        if (sid >= 0) {
            ps->p++;
            if (g_structs[sid].generic) {        /* `Box(int)` in type position: explicit type args -> a concrete instance */
                eat(ps, TK_LPAREN, "'(' with the type arguments for a generic struct");
                int np = g_structs[sid].ntyparams;
                Type args[8];
                for (int i = 0; i < np; i++) {
                    args[i] = parse_type(ps);
                    if (i + 1 < np) eat(ps, TK_COMMA, "',' between type arguments");
                }
                eat(ps, TK_RPAREN, "')' after the type arguments");
                /* self/recursive reference: the generic applied to exactly its own type
                 * parameters (`LL($T)` inside `struct LL($T)`). Defer -- keep the generic
                 * template type; subst_type concretizes it when the instance is built. */
                int self_ref = 1;
                for (int i = 0; i < np; i++) if (args[i] != g_structs[sid].typarams[i]) { self_ref = 0; break; }
                if (self_ref) return STRUCT_TYPE(sid);
                for (int i = 0; i < np; i++)
                    if (has_typaram(args[i]))
                        die_at(t->line, "generic struct '%s': a type argument may not partially mention a type "
                               "parameter; use the generic applied to its own parameters (a recursive reference) "
                               "or to concrete types", g_structs[sid].name);
                Type binds[256];
                for (int i = 0; i < g_ntyparams; i++) binds[i] = T_VOID;
                for (int i = 0; i < np; i++) binds[(int)(g_structs[sid].typarams[i] - T_TYPARAM_BASE)] = args[i];
                return STRUCT_TYPE(struct_instantiate(sid, binds));
            }
            return STRUCT_TYPE(sid);
        }
        int eid = enum_find(nm);
        if (eid >= 0) {
            ps->p++;
            if (g_enums[eid].generic) {        /* `Tree(int)` in type position: explicit type args -> a concrete instance */
                eat(ps, TK_LPAREN, "'(' with the type arguments for a generic enum");
                int np = g_enums[eid].ntyparams;
                Type args[8];
                for (int i = 0; i < np; i++) {
                    args[i] = parse_type(ps);
                    if (i + 1 < np) eat(ps, TK_COMMA, "',' between type arguments");
                }
                eat(ps, TK_RPAREN, "')' after the type arguments");
                /* self/recursive reference: the generic applied to exactly its own type
                 * parameters (`Tree($T)` inside `enum Tree($T)`). Defer -- keep the generic
                 * template type; subst_type concretizes it when the instance is built. */
                int self_ref = 1;
                for (int i = 0; i < np; i++) if (args[i] != g_enums[eid].typarams[i]) { self_ref = 0; break; }
                if (self_ref) return ENUM_TYPE(eid);
                for (int i = 0; i < np; i++)
                    if (has_typaram(args[i]))
                        die_at(t->line, "generic enum '%s': a type argument may not partially mention a type "
                               "parameter; use the generic applied to its own parameters (a recursive reference) "
                               "or to concrete types", g_enums[eid].name);
                Type binds[256];
                for (int i = 0; i < g_ntyparams; i++) binds[i] = T_VOID;
                for (int i = 0; i < np; i++) binds[(int)(g_enums[eid].typarams[i] - T_TYPARAM_BASE)] = args[i];
                return ENUM_TYPE(enum_instantiate(eid, binds));
            }
            return ENUM_TYPE(eid);
        }
        int nid = newtype_find(nm);
        if (nid >= 0) { ps->p++; return NT_TYPE(nid); }
        int hid = handle_find(nm);
        if (hid >= 0) { ps->p++; return T_HANDLE_BASE + hid; }
        const char *sg = suggest_type(nm);
        if (sg) die_at(t->line, "unknown type '%s'; did you mean '%s'?", t->text, sg);
        die_at(t->line, "unknown type '%s'", t->text);
    }
    switch (t->kind) {
        case TK_KW_INT:    ps->p++; return T_INT;
        case TK_KW_FLOAT:  ps->p++; return T_FLOAT;
        case TK_KW_BOOL:   ps->p++; return T_BOOL;
        case TK_KW_STRING: ps->p++; return T_STRING;
        case TK_KW_PTR:    ps->p++; return T_PTR;
        case TK_KW_BYTES:  ps->p++; return T_BYTES;
        case TK_KW_U32:    ps->p++; return T_U32;
        case TK_KW_U64:    ps->p++; return T_U64;
        case TK_KW_F32:    ps->p++; return T_F32;
        default: die_at(t->line, "expected a type (int, float, bool, string, [int], or a struct)");
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
static Expr *parse_postfix(Parser *ps);   /* spawn parses its callee through the postfix chain */
static int   is_literal_expr(Expr *e);    /* const RHS validation (plain int/float/str/bool/char literal) */
static Expr *consts_find(const char *name);
static Expr *const_fold(Expr *e, int refs); /* fold a const-expr (int arith/bitwise/unary + backward const refs when refs) to one literal */

/* String interpolation (parse-time desugar; no new AST node): "a{e}b" becomes
 * ("a" + str(e) + "b"). `{{`/`}}` are literal braces. Each `{e}` is lexed+parsed as a
 * sub-expression and wrapped in str() (identity on a string, i2s/f2s on int/float —
 * a bool/char/aggregate hole then fails str() with a clear error). */
static Expr *interp_join(Expr *acc, Expr *piece, int line) {
    if (!acc) return piece;
    Expr *b = new_expr(E_BINOP, line); b->op = TK_PLUS; b->lhs = acc; b->rhs = piece; return b;
}
static Expr *desugar_interp(const char *s, int line) {
    Expr *acc = NULL;
    char *buf = (char *)xmalloc(strlen(s) + 1); size_t bn = 0;
    for (size_t i = 0; s[i]; ) {
        if (s[i] == '{' && s[i+1] == '{') { buf[bn++] = '{'; i += 2; continue; }
        if (s[i] == '}' && s[i+1] == '}') { buf[bn++] = '}'; i += 2; continue; }
        if (s[i] == '}') die_at(line, "unmatched '}' in an interpolated string (use '}}' for a literal brace)");
        if (s[i] == '{') {
            if (bn) { Expr *lit = new_expr(E_STR, line); lit->sval = xstrndup(buf, bn); acc = interp_join(acc, lit, line); bn = 0; }
            i++; size_t start = i;
            int depth = 1;                       /* balance nested braces; skip nested string literals */
            while (s[i] && depth > 0) {
                if (s[i] == '"') {               /* a string literal inside the hole — '}' within it is not a hole end */
                    i++;
                    while (s[i] && s[i] != '"') { if (s[i] == '\\' && s[i+1]) i++; i++; }
                    if (!s[i]) die_at(line, "unterminated string in an interpolated hole");
                    i++; continue;
                }
                if (s[i] == '{') depth++;
                else if (s[i] == '}') { depth--; if (depth == 0) break; }
                i++;
            }
            if (depth != 0) die_at(line, "unterminated '{' in an interpolated string");
            if (i == start) die_at(line, "empty '{}' in an interpolated string");
            char *sub = xstrndup(s + start, i - start); i++;     /* consume '}' */
            /* lex(sub) clobbers g_src (die_at's source-line snippet); restore it after
             * parsing the hole so later diagnostics in the file keep their snippet.
             * (die_at also reads g_srcname and g_err_col; lex touches neither on a
             * non-fatal path.) */
            const char *save_src = g_src;
            TokVec tv = lex(sub); Parser sp = { tv.v, 0, 0 }; Expr *ex = parse_expr(&sp);
            g_src = save_src;
            Expr *call = new_expr(E_CALL, line); call->sval = "str";
            call->args = (Expr **)xmalloc(sizeof(Expr *)); call->args[0] = ex; call->nargs = 1;
            acc = interp_join(acc, call, line);
            continue;
        }
        buf[bn++] = s[i++];
    }
    if (bn) { Expr *lit = new_expr(E_STR, line); lit->sval = xstrndup(buf, bn); acc = interp_join(acc, lit, line); }
    if (!acc) { Expr *e = new_expr(E_STR, line); e->sval = ""; return e; }
    return acc;
}

static Stmt *new_stmt(StmtKind k, int line);   /* forward: the lambda builds a Return/Expr body stmt */

static Expr *parse_primary(Parser *ps) {
    Tok *t = cur(ps);
    if (t->kind == TK_FN) {   /* lambda: fn(p: T, ...) [-> R]: expr  (a closure literal -> E_LAMBDA).
                               * The lifted proc's params are filled with [captures][lambda params] at
                               * resolve, once the enclosing scope is known (capture analysis). */
        ps->p++;
        eat(ps, TK_LPAREN, "'(' after fn in a lambda");
        Proc *pr = (Proc *)xmalloc(sizeof(Proc));
        memset(pr, 0, sizeof *pr);
        TBL_ENSURE(g_laminfo, g_nlaminfo, g_laminfo_cap);
        int id = g_nlaminfo++;   /* reserve this id BEFORE the body (a nested lambda takes id+1) */
        pr->name = sfmt("__lam%d", id);
        pr->line = t->line;
        int cap = 0;
        while (!at(ps, TK_RPAREN)) {
            Tok *pn = eat(ps, TK_IDENT, "a lambda parameter name");
            Type pt = T_VOID;           /* B-2: an untyped param is filled from the expected fn type at resolve */
            if (accept(ps, TK_COLON))
                pt = parse_type(ps);    /* lambdas take by-value params only */
            if (pr->nparams == cap) { cap = cap ? cap * 2 : 4; pr->params = (Param *)xrealloc(pr->params, (size_t)cap * sizeof(Param)); }
            pr->params[pr->nparams].name = pn->text;
            pr->params[pr->nparams].type = pt;
            pr->params[pr->nparams].is_inout = 0;
            pr->params[pr->nparams].is_sink = 0;
            pr->params[pr->nparams].ffi_ct = NULL;
            pr->nparams++;
            if (!accept(ps, TK_COMMA)) break;
        }
        eat(ps, TK_RPAREN, "')'");
        if (accept(ps, TK_ARROW)) { pr->ret = parse_type(ps); pr->has_ret = 1; }
        else pr->ret = T_VOID;
        eat(ps, TK_COLON, "':' before the lambda body expression");
        Expr *body = parse_expr(ps);
        Stmt *s = new_stmt(pr->ret == T_VOID ? S_EXPR : S_RETURN, t->line);
        s->expr = body;
        pr->body = (Stmt **)xmalloc(sizeof(Stmt *));
        pr->body[0] = s; pr->nbody = 1;
        Expr *e = new_expr(E_LAMBDA, t->line);
        e->ival = id;
        g_laminfo[id].proc = pr;
        g_laminfo[id].ncap = 0;
        g_laminfo[id].ftype = T_VOID;
        return e;
    }
    if (t->kind == TK_INT)  { ps->p++; Expr *e = new_expr(E_INT, t->line);  e->ival = t->ival; return e; }
    if (t->kind == TK_CHAR) { ps->p++; Expr *e = new_expr(E_CHAR, t->line); e->ival = t->ival; return e; }
    if (t->kind == TK_FLOAT){ ps->p++; Expr *e = new_expr(E_FLOAT, t->line); e->fval = t->fval; return e; }
    if (t->kind == TK_STR)  {
        ps->p++;
        if (t->ival) return desugar_interp(t->text, t->line);   /* f"..." interpolated string */
        Expr *e = new_expr(E_STR, t->line);  e->sval = t->text; return e;
    }
    if (t->kind == TK_TRUE) { ps->p++; Expr *e = new_expr(E_BOOL, t->line); e->ival = 1; return e; }
    if (t->kind == TK_FALSE){ ps->p++; Expr *e = new_expr(E_BOOL, t->line); e->ival = 0; return e; }
    if (t->kind == TK_NULL) { ps->p++; return new_expr(E_NULL, t->line); }   /* the opaque ptr literal */
    if (t->kind == TK_LPAREN) {
        ps->p++;
        Expr *first = parse_expr(ps);
        if (!at(ps, TK_COMMA)) {         /* plain grouping ( expr ) */
            eat(ps, TK_RPAREN, "')'");
            return first;
        }
        Expr *e = new_expr(E_TUPLE, t->line);   /* tuple literal (e1, e2, ...) */
        int cap = 4; e->args = (Expr **)xrealloc(e->args, (size_t)cap * sizeof(Expr *));
        e->args[e->nargs++] = first;
        while (accept(ps, TK_COMMA)) {
            if (at(ps, TK_RPAREN)) break;       /* trailing comma */
            if (e->nargs == cap) { cap *= 2; e->args = (Expr **)xrealloc(e->args, (size_t)cap * sizeof(Expr *)); }
            e->args[e->nargs++] = parse_expr(ps);
        }
        eat(ps, TK_RPAREN, "')'");
        if (e->nargs > 8) die_at(t->line, "a tuple has at most 8 elements");
        return e;
    }
    if (t->kind == TK_LBRACKET) {            /* array or map literal */
        ps->p++;
        Expr *e = new_expr(E_ARRLIT, t->line);
        if (at(ps, TK_RBRACKET)) {           /* empty: []int / []string / []string: int / bare [] (typed by context) */
            ps->p++;
            /* B-0 (bidirectional inference): a `[]` NOT followed by a type-starter
             * token is a bare empty literal; resolve_exp grounds it from the
             * expected type (checking mode), or dies with a local error. */
            TokKind nk = cur(ps)->kind;
            if (nk != TK_KW_INT && nk != TK_KW_BOOL && nk != TK_KW_STRING && nk != TK_KW_FLOAT
                && nk != TK_KW_PTR && nk != TK_KW_BYTES && nk != TK_KW_U32 && nk != TK_KW_U64 && nk != TK_KW_F32
                && nk != TK_IDENT && nk != TK_LBRACKET && nk != TK_LPAREN && nk != TK_FN) {
                e->ival = T_VOID;            /* the "untyped" marker */
                return e;
            }
            Type elem = parse_type(ps);
            if (at(ps, TK_COLON)) {          /* empty map literal []K: V */
                ps->p++;
                Type val = parse_type(ps);
                Type mt = map_of(elem, val);
                if (mt == T_VOID) die_at(t->line, "map keys must be string, int (directly or through a newtype), or a fieldless enum; int-keyed maps support only int/float values");
                e->ival = mt; e->op = TK_COLON;
                return e;
            }
            if (elem == T_VOID || elem == T_BOOL)
                die_at(t->line, "array elements must be int, float, string, a struct, or an array");
            e->ival = arr_of(elem);   /* type carried to the resolver */
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
            cap = 4; e->args = (Expr **)xrealloc(e->args, (size_t)cap * sizeof(Expr *));
            e->args[e->nargs++] = first;             /* key0 */
            e->args[e->nargs++] = parse_expr(ps);    /* val0 */
            while (accept(ps, TK_COMMA)) {
                if (at(ps, TK_RBRACKET)) break;       /* trailing comma */
                if (e->nargs + 2 > cap) { cap *= 2; e->args = (Expr **)xrealloc(e->args, (size_t)cap * sizeof(Expr *)); }
                e->args[e->nargs++] = parse_expr(ps);   /* key */
                eat(ps, TK_COLON, "':' in a map literal entry");
                e->args[e->nargs++] = parse_expr(ps);   /* value */
            }
            eat(ps, TK_RBRACKET, "']'");
            return e;
        }
        cap = 4; e->args = (Expr **)xrealloc(e->args, (size_t)cap * sizeof(Expr *));
        e->args[e->nargs++] = first;
        while (accept(ps, TK_COMMA)) {
            if (at(ps, TK_RBRACKET)) break;          /* trailing comma */
            if (e->nargs == cap) { cap = cap * 2; e->args = (Expr **)xrealloc(e->args, (size_t)cap * sizeof(Expr *)); }
            e->args[e->nargs++] = parse_expr(ps);
        }
        eat(ps, TK_RBRACKET, "']'");
        return e;
    }
    if (t->kind == TK_SPAWN) {             /* spawn f(args): run the call on a new thread */
        ps->p++;
        Expr *call = parse_postfix(ps);
        if (call->kind != E_CALL)
            die_at(t->line, "spawn requires a direct call: spawn f(args)");
        Expr *e = new_expr(E_SPAWN, t->line);
        e->lhs = call;
        return e;
    }
    if (t->kind == TK_IDENT) {
        ps->p++;
        if (!strcmp(t->text, "soa")) {     /* soa []Struct : an empty SOA literal */
            eat(ps, TK_LBRACKET, "'[' after soa");
            eat(ps, TK_RBRACKET, "']' (an empty soa literal is `soa []Struct`)");
            Type el = parse_type(ps);
            if (!IS_STRUCT(el)) die_at(t->line, "soa requires a struct element type, e.g. soa []Point");
            Expr *e = new_expr(E_ARRLIT, t->line);
            e->ival = soa_of(el);          /* empty: type carried to the resolver */
            return e;
        }
        if (!strcmp(t->text, "channel") && at(ps, TK_LPAREN)) {   /* channel(T, cap): create a bounded queue (CC-4) */
            ps->p++;
            Expr *e = new_expr(E_CALL, t->line);
            e->sval = "channel";
            e->ival = (long)parse_type(ps);     /* the element type rides in ival */
            eat(ps, TK_COMMA, "',' between the element type and the capacity");
            e->args = (Expr **)xmalloc(sizeof(Expr *));
            e->args[0] = parse_expr(ps); e->nargs = 1;
            eat(ps, TK_RPAREN, "')'");
            e->pkg = g_cur_pkg_prefix;
            return e;
        }
        if (!strcmp(t->text, "None"))      /* the bare None literal */
            return new_expr(E_NONE, t->line);
        if (!strcmp(t->text, "Some")) {    /* Some(value) */
            eat(ps, TK_LPAREN, "'(' after Some");
            Expr *e = new_expr(E_SOME, t->line);
            e->lhs = parse_expr(ps);
            eat(ps, TK_RPAREN, "')'");
            return e;
        }
        if (!strcmp(t->text, "Ok") || !strcmp(t->text, "Err")) {   /* Ok(v) / Err(e) */
            int isok = !strcmp(t->text, "Ok");
            eat(ps, TK_LPAREN, isok ? "'(' after Ok" : "'(' after Err");
            Expr *e = new_expr(isok ? E_OK : E_ERR, t->line);
            e->lhs = parse_expr(ps);
            eat(ps, TK_RPAREN, "')'");
            return e;
        }
        if (at(ps, TK_DOLLAR)) {           /* generics: explicit type args -- name$(T1, ...) [ (value args) ] */
            ps->p++;
            eat(ps, TK_LPAREN, "'(' after '$' for explicit type arguments");
            Expr *e = new_expr(E_CALL, t->line);
            e->sval = t->text;
            e->pkg  = g_cur_pkg_prefix;
            Type tas[16]; int nta = 0;
            if (!at(ps, TK_RPAREN)) {
                tas[nta++] = parse_type(ps);
                while (accept(ps, TK_COMMA)) {
                    if (nta >= 16) die_at(t->line, "at most 16 explicit type arguments");
                    tas[nta++] = parse_type(ps);
                }
            }
            eat(ps, TK_RPAREN, "')' after explicit type arguments");
            if (nta == 0) die_at(t->line, "'%s$()' needs at least one explicit type argument", t->text);
            e->typeargs = (Type *)xmalloc((size_t)nta * sizeof(Type));
            for (int i = 0; i < nta; i++) e->typeargs[i] = tas[i];
            e->ntypeargs = nta;
            if (accept(ps, TK_LPAREN)) {   /* the value-arg list is optional (absent => no value args) */
                int cap = 0;
                while (!at(ps, TK_RPAREN)) {
                    if (e->nargs == cap) { cap = cap ? cap * 2 : 4; e->args = (Expr **)xrealloc(e->args, (size_t)cap * sizeof(Expr *)); }
                    e->args[e->nargs++] = parse_expr(ps);
                    if (!accept(ps, TK_COMMA)) break;
                }
                eat(ps, TK_RPAREN, "')'");
            }
            return e;
        }
        if (at(ps, TK_LPAREN)) {           /* call */
            ps->p++;
            /* B5: the map_* mutators were removed in favour of operator/keyword
             * syntax. Reject a user-typed call here (the `delete` desugar builds
             * its map_del node directly, bypassing this path; map_get is kept). */
            if (!strcmp(t->text, "map_set")) die_at(t->line, "map_set was removed; use `m[k] = v`");
            if (!strcmp(t->text, "map_has")) die_at(t->line, "map_has was removed; use `k in m`");
            if (!strcmp(t->text, "map_del")) die_at(t->line, "map_del was removed; use `delete m[k]`");
            Expr *e = new_expr(E_CALL, t->line);
            e->sval = t->text;
            e->pkg  = g_cur_pkg_prefix;   /* the package this call appears in; resolver tries <pkg>name first */
            int cap = 0;
            while (!at(ps, TK_RPAREN)) {
                if (e->nargs == cap) {
                    cap = cap ? cap * 2 : 4;
                    e->args = (Expr **)xrealloc(e->args, (size_t)cap * sizeof(Expr *));
                }
                e->args[e->nargs++] = parse_expr(ps);
                if (!accept(ps, TK_COMMA)) break;
            }
            eat(ps, TK_RPAREN, "')'");
            return e;
        }
        Expr *e = new_expr(E_IDENT, t->line);  /* variable (or a bare payload-less enum variant) */
        e->sval = t->text;
        e->pkg  = g_cur_pkg_prefix;            /* lets a package-local bare variant resolve */
        return e;
    }
    g_err_col = t->col;
    die_at(t->line, "expected an expression");
    return NULL;
}

/* postfix: primary ( '[' expr ']' | '.' field )* */
static Expr *parse_postfix(Parser *ps) {
    Expr *e = parse_primary(ps);
    for (;;) {
        if (at(ps, TK_LBRACKET)) {
            Tok *t = cur(ps); ps->p++;
            /* xs[i] is an index; xs[a:b] / xs[a:] / xs[:b] / xs[:] is a slice.
             * A ':' anywhere inside the brackets makes it a slice; either bound
             * may be omitted (lo defaults to 0, hi to len). */
            Expr *lo = NULL, *hi = NULL; int is_slice = 0;
            if (at(ps, TK_COLON)) { is_slice = 1; ps->p++; if (!at(ps, TK_RBRACKET)) hi = parse_expr(ps); }
            else {
                lo = parse_expr(ps);
                if (at(ps, TK_COLON)) { is_slice = 1; ps->p++; if (!at(ps, TK_RBRACKET)) hi = parse_expr(ps); }
            }
            eat(ps, TK_RBRACKET, "']'");
            if (is_slice) {
                Expr *sl = new_expr(E_SLICE, t->line);
                sl->lhs = e; sl->rhs = lo;          /* lo NULL => 0 */
                if (hi) { sl->args = (Expr **)xrealloc(sl->args, sizeof(Expr *)); sl->args[0] = hi; sl->nargs = 1; }
                e = sl;
            } else {
                Expr *ix = new_expr(E_INDEX, t->line);
                ix->lhs = e; ix->rhs = lo;
                e = ix;
            }
        } else if (at(ps, TK_DOT)) {
            Tok *t = cur(ps); ps->p++;
            if (at(ps, TK_INT)) {              /* tuple index: t.0 / t.1 */
                Tok *n = cur(ps); ps->p++;
                Expr *ti = new_expr(E_TUPIDX, t->line);
                ti->lhs = e; ti->ival = n->ival;
                e = ti;
            } else {
                Tok *f = eat(ps, TK_IDENT, "a field name or tuple index after '.'");
                if (at(ps, TK_LPAREN) && e->kind == E_IDENT) {
                    /* `pkg.name(args)` — a qualified call. tycho has no methods, so a
                     * field followed by `(` on a bare identifier is always a package
                     * call; the qualifier resolves to a package prefix in the resolver. */
                    ps->p++;
                    Expr *c = new_expr(E_CALL, t->line);
                    c->sval = f->text;
                    c->qual = e->sval;            /* the qualifier ident, e.g. "geom" */
                    c->pkg  = g_cur_pkg_prefix;
                    int cap = 0;
                    while (!at(ps, TK_RPAREN)) {
                        if (c->nargs == cap) { cap = cap ? cap * 2 : 4; c->args = (Expr **)xrealloc(c->args, (size_t)cap * sizeof(Expr *)); }
                        c->args[c->nargs++] = parse_expr(ps);
                        if (!accept(ps, TK_COMMA)) break;
                    }
                    eat(ps, TK_RPAREN, "')'");
                    e = c;
                } else {
                    Expr *fe = new_expr(E_FIELD, t->line);
                    fe->lhs = e; fe->sval = f->text;
                    e = fe;
                }
            }
        } else if (at(ps, TK_LPAREN)) {
            /* call-on-expression: `<expr>(args)` — an indirect call on a fn VALUE
             * that is the result of an index / field / prior call (e.g. xs[i](a),
             * h.cb(a), f(a)(b)). E_CALL with lhs=callee and no sval distinguishes
             * it from a named call (whose sval is the function name). */
            Tok *t = cur(ps); ps->p++;
            Expr *c = new_expr(E_CALL, t->line);
            c->lhs = e;                  /* the callee expression (sval stays NULL) */
            c->pkg = g_cur_pkg_prefix;
            int cap = 0;
            while (!at(ps, TK_RPAREN)) {
                if (c->nargs == cap) { cap = cap ? cap * 2 : 4; c->args = (Expr **)xrealloc(c->args, (size_t)cap * sizeof(Expr *)); }
                c->args[c->nargs++] = parse_expr(ps);
                if (!accept(ps, TK_COMMA)) break;
            }
            eat(ps, TK_RPAREN, "')'");
            e = c;
        } else break;
    }
    if (at(ps, TK_ORRETURN)) {   /* postfix: binds tighter than any binary op */
        Tok *t = cur(ps); ps->p++;
        Expr *o = new_expr(E_ORRETURN, t->line);
        o->lhs = e;
        e = o;
    }
    return e;
}

static Expr *parse_unary_inner(Parser *ps);
/* Bound expression-nesting recursion: every nesting level (parentheses and
 * unary chains) passes through parse_unary, so guarding here caps total
 * parser recursion and stops a crafted deeply-nested expression from
 * overflowing the stack (SIGSEGV). Statement nesting is already bounded by
 * the lexer's indentation-depth cap. */
static Expr *parse_unary(Parser *ps) {
    if (++ps->depth > TYCHO_MAX_PARSE_DEPTH)
        die_at(cur(ps)->line, "expression nesting too deep");
    Expr *e = parse_unary_inner(ps);
    ps->depth--;
    return e;
}
static Expr *parse_unary_inner(Parser *ps) {
    if (at(ps, TK_MINUS)) {                /* unary negation: operand in lhs, rhs NULL */
        Tok *t = cur(ps); ps->p++;
        Expr *e = new_expr(E_BINOP, t->line);
        e->op = TK_MINUS; e->lhs = parse_unary(ps);
        return e;
    }
    if (at(ps, TK_AMP)) {                  /* &lvalue — a mut argument */
        Tok *t = cur(ps); ps->p++;
        Expr *e = new_expr(E_ADDR, t->line);
        e->lhs = parse_unary(ps);
        return e;
    }
    if (at(ps, TK_TILDE)) {                /* unary bitwise NOT: operand in lhs, rhs NULL */
        Tok *t = cur(ps); ps->p++;
        Expr *e = new_expr(E_BINOP, t->line);
        e->op = TK_TILDE; e->lhs = parse_unary(ps);
        return e;
    }
    return parse_postfix(ps);
}

static Expr *parse_mul(Parser *ps) {
    Expr *l = parse_unary(ps);
    while (at(ps, TK_STAR) || at(ps, TK_SLASH) || at(ps, TK_PERCENT) ||
           at(ps, TK_SHL)  || at(ps, TK_SHR)   || at(ps, TK_AMP)) {
        Tok *t = cur(ps); ps->p++;
        Expr *e = new_expr(E_BINOP, t->line);
        e->op = t->kind; e->lhs = l; e->rhs = parse_unary(ps);
        l = e;
    }
    return l;
}

static Expr *parse_add(Parser *ps) {
    Expr *l = parse_mul(ps);
    while (at(ps, TK_PLUS) || at(ps, TK_MINUS) || at(ps, TK_PIPE) || at(ps, TK_CARET)) {
        Tok *t = cur(ps); ps->p++;
        Expr *e = new_expr(E_BINOP, t->line);
        e->op = t->kind; e->lhs = l; e->rhs = parse_mul(ps);
        l = e;
    }
    return l;
}

static Expr *parse_cmp(Parser *ps) {            /* comparison level */
    Expr *l = parse_add(ps);
    while (at(ps, TK_EQEQ) || at(ps, TK_NEQ) || at(ps, TK_LT) ||
           at(ps, TK_GT)   || at(ps, TK_LE)  || at(ps, TK_GE) || at(ps, TK_IN)) {
        Tok *t = cur(ps); ps->p++;
        Expr *e = new_expr(E_BINOP, t->line);
        e->op = t->kind; e->lhs = l; e->rhs = parse_add(ps);
        l = e;
    }
    return l;
}

/* Logical operators, conventional precedence (tighter binds first):
 * comparisons > not > and > or. `not` is unary (operand in lhs, rhs NULL);
 * `and`/`or` short-circuit, lowering directly to C's && / ||. */
static Expr *parse_not(Parser *ps) {
    if (at(ps, TK_NOT)) {
        Tok *t = cur(ps); ps->p++;
        Expr *e = new_expr(E_BINOP, t->line);
        e->op = TK_NOT; e->lhs = parse_not(ps);   /* rhs stays NULL (zeroed) */
        return e;
    }
    return parse_cmp(ps);
}

static Expr *parse_and(Parser *ps) {
    Expr *l = parse_not(ps);
    while (at(ps, TK_AND)) {
        Tok *t = cur(ps); ps->p++;
        Expr *e = new_expr(E_BINOP, t->line);
        e->op = TK_AND; e->lhs = l; e->rhs = parse_not(ps);
        l = e;
    }
    return l;
}

static Expr *parse_expr(Parser *ps) {           /* logical-or: the top level */
    Expr *l = parse_and(ps);
    while (at(ps, TK_OR)) {
        Tok *t = cur(ps); ps->p++;
        Expr *e = new_expr(E_BINOP, t->line);
        e->op = TK_OR; e->lhs = l; e->rhs = parse_and(ps);
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

/* `if` / `elif` / `else`. `elif` is sugar for `else:` containing a single
 * nested `if`, so the existing S_IF codegen (which already emits `else { ... }`
 * around the else-block) needs no special case. */
static Stmt *parse_if(Parser *ps, int line) {
    Stmt *s = new_stmt(S_IF, line);
    s->expr = parse_expr(ps);
    eat(ps, TK_COLON, "':' before the block");
    eat(ps, TK_NEWLINE, "newline");
    s->body = parse_block(ps, &s->nbody);
    if (at(ps, TK_ELIF)) {
        Tok *e = cur(ps); ps->p++;
        s->els = (Stmt **)xmalloc(sizeof(Stmt *));
        s->els[0] = parse_if(ps, e->line);   /* the elif, as the whole else-branch */
        s->nels = 1;
    } else if (at(ps, TK_ELSE)) {
        ps->p++;
        eat(ps, TK_COLON, "':' after else");
        eat(ps, TK_NEWLINE, "newline after else");
        s->els = parse_block(ps, &s->nels);
    }
    return s;
}

/* --- expression-valued if/match (ROADMAP 2.1) -------------------------------
 * `x := if c: a else: b`, `x = match v: ...`, `return if c: a else: b`, etc.
 * Restricted to TAIL position (RHS of :=/typed-:=/=/place-=/return) and to a
 * SINGLE expression per branch/arm. Each branch's tail expression is parsed as
 * a lone `S_EXPR` (the normal statement grammar rejects a bare non-call expr).
 * The non-decl positions desugar at parse time into ordinary S_RETURN/S_ASSIGN/
 * S_INDEXSET/S_FIELDSET inside the branches (so resolve+codegen are reused
 * wholesale); only `:=`/typed-`:=` keeps the control node on S_DECL.ctrl, its
 * tails rewritten to assignments once the inferred type is known (resolver).
 * ponytail: single-expression branches only; multi-statement value branches,
 * diverging arms, and nested-subexpression use are deliberate follow-ups. */
static Stmt **parse_value_block(Parser *ps, int *count) {
    eat(ps, TK_INDENT, "an indented value branch");
    Expr *tail = parse_expr(ps);
    if (!at(ps, TK_NEWLINE))
        die_at(tail->line, "a value branch must be a single expression "
               "(multi-statement value branches are not yet supported)");
    eat(ps, TK_NEWLINE, "newline");
    while (accept(ps, TK_NEWLINE)) {}
    eat(ps, TK_DEDENT, "end of the value branch");
    Stmt *se = new_stmt(S_EXPR, tail->line);
    se->expr = tail;
    Stmt **body = (Stmt **)xmalloc(sizeof(Stmt *));
    body[0] = se; *count = 1;
    return body;
}

static Stmt *parse_value_if(Parser *ps, int line) {
    Stmt *s = new_stmt(S_IF, line);
    s->expr = parse_expr(ps);
    eat(ps, TK_COLON, "':' before the block");
    eat(ps, TK_NEWLINE, "newline");
    s->body = parse_value_block(ps, &s->nbody);
    if (at(ps, TK_ELIF)) {
        Tok *e = cur(ps); ps->p++;
        s->els = (Stmt **)xmalloc(sizeof(Stmt *));
        s->els[0] = parse_value_if(ps, e->line);
        s->nels = 1;
    } else if (at(ps, TK_ELSE)) {
        ps->p++;
        eat(ps, TK_COLON, "':' after else");
        eat(ps, TK_NEWLINE, "newline after else");
        s->els = parse_value_block(ps, &s->nels);
    } else {
        die_at(line, "an `if` used as a value must have an `else` — every path must produce a value");
    }
    return s;
}

/* the `match` parser, shared by the statement form (value=0, block arms) and the
 * value form (value=1, single-expression arms). Assumes `match` is consumed. */
static Stmt *parse_match(Parser *ps, int line, int value) {
    Stmt *s = new_stmt(S_MATCH, line);
    s->expr = parse_expr(ps);                 /* the Option/enum being matched */
    eat(ps, TK_COLON, "':' before the match arms");
    eat(ps, TK_NEWLINE, "newline");
    eat(ps, TK_INDENT, "indented match arms");
    int cap = 0;
    while (!at(ps, TK_DEDENT) && !at(ps, TK_EOF)) {
        if (accept(ps, TK_NEWLINE)) continue;
        Tok *vn = eat(ps, TK_IDENT, "a match arm `Variant(bindings):` or `Variant:`");
        const char *vqual = NULL, *vname = vn->text;
        if (accept(ps, TK_DOT)) {           /* qualified `pkg.Variant:` */
            vqual = vn->text;
            vname = eat(ps, TK_IDENT, "a variant name after the package qualifier")->text;
        }
        if (s->narms == cap) { cap = cap ? cap * 2 : 4; s->arms = (MatchArm *)xrealloc(s->arms, (size_t)cap * sizeof(MatchArm)); }
        MatchArm *arm = &s->arms[s->narms++];
        if (vqual) {
            check_pkg_private(vqual, vname, vn->line);
            arm->variant = sfmt("%s%s", pkg_prefix_for(vqual), vname);
        }
        else if (!strcmp(vname, "_"))
            arm->variant = (char *)vname;
        else if (!strcmp(vname, "Some") || !strcmp(vname, "None") || !strcmp(vname, "Ok") || !strcmp(vname, "Err"))
            arm->variant = (char *)vname;
        else
            arm->variant = pkg_mangle(vname);
        arm->nbinds = 0; arm->line = vn->line;
        if (accept(ps, TK_LPAREN)) {
            while (!at(ps, TK_RPAREN)) {
                if (arm->nbinds >= 8) die_at(vn->line, "too many bindings (max 8)");
                arm->binds[arm->nbinds++] = eat(ps, TK_IDENT, "a binding name")->text;
                if (!accept(ps, TK_COMMA)) break;
            }
            eat(ps, TK_RPAREN, "')'");
        }
        eat(ps, TK_COLON, "':' after the arm pattern");
        eat(ps, TK_NEWLINE, "newline");
        arm->body = value ? parse_value_block(ps, &arm->nbody)
                          : parse_block(ps, &arm->nbody);
    }
    eat(ps, TK_DEDENT, "end of the match arms");
    if (s->narms == 0) die_at(line, "match needs at least one arm");
    return s;
}

/* parse an `if`/`match` in value (tail) position; caller is positioned ON the
 * `if`/`match` keyword. Returns the S_IF/S_MATCH with single-expr branch tails. */
static Stmt *parse_value_ctrl(Parser *ps) {
    Tok *t = cur(ps);
    if (t->kind == TK_IF)    { ps->p++; return parse_value_if(ps, t->line); }
    if (t->kind == TK_MATCH) { ps->p++; return parse_match(ps, t->line, 1); }
    die_at(t->line, "expected `if` or `match`");
    return NULL;
}

/* rewrite the single-expr tail of every branch/arm of a value if/match into a
 * concrete statement (S_RETURN / S_ASSIGN / place-set) — the parse-time desugar
 * for the non-declaration tail positions. `kind` is the target StmtKind;
 * `name`/`target` supply the destination. Recurses through elif chains. */
static void ctrl_rewrite_tails(Stmt *c, StmtKind kind, char *name, Expr *target) {
    Stmt **branches[2]; int nbr = 0;
    if (c->kind == S_IF) {
        branches[nbr++] = c->body;
        if (c->nels == 1 && c->els[0]->kind == S_IF) { ctrl_rewrite_tails(c->els[0], kind, name, target); }
        else branches[nbr++] = c->els;
    }
    /* for a match, each arm is a branch */
    int narm = (c->kind == S_MATCH) ? c->narms : 0;
    for (int i = 0; i < nbr + narm; i++) {
        Stmt **body = (i < nbr) ? branches[i] : c->arms[i - nbr].body;
        Stmt *se = body[0];                         /* the S_EXPR(tail) — always index 0, one element */
        Stmt *ns = new_stmt(kind, se->line);
        if (kind == S_RETURN)      { ns->expr = se->expr; }
        else if (kind == S_ASSIGN) { ns->name = name; ns->expr = se->expr; }
        else                       { ns->target = target; ns->expr = se->expr; }  /* S_INDEXSET / S_FIELDSET */
        body[0] = ns;
    }
}

/* collect the (already-resolved) tail expression of every branch/arm — used to
 * unify their types for a `:=` value if/match. Mirrors ctrl_rewrite_tails. */
static void ctrl_collect_tails(Stmt *c, Expr **out, int *n) {
    if (c->kind == S_IF) {
        out[(*n)++] = c->body[0]->expr;
        if (c->nels == 1 && c->els[0]->kind == S_IF) ctrl_collect_tails(c->els[0], out, n);
        else out[(*n)++] = c->els[0]->expr;
    } else {   /* S_MATCH */
        for (int i = 0; i < c->narms; i++) out[(*n)++] = c->arms[i].body[0]->expr;
    }
}

/* `for x in COLL:` desugars to a collection-temp decl plus a range loop. The
 * temp decl must land in the block BEFORE the loop; parse_stmt queues it here
 * and parse_block drains the queue ahead of the statement it returned. */
static Stmt *g_pending[64];
static int g_npending = 0;
static int g_forin_uid = 0;
/* set for exactly the `for` directly under a `parallel` keyword, so its foreach
 * form is deferred (type-directed in resolve_parfor) instead of array-desugared;
 * the TK_FOR handler consumes it immediately so a nested sequential foreach in
 * the parfor body is unaffected. */
static int g_parallel_ctx = 0;

/* the binary operators that have a compound-assignment form `x OP= e`. Detected
 * as the operator token followed by `=` (no dedicated token needed). */
static int is_compound_op(TokKind k) {
    return k == TK_PLUS || k == TK_MINUS || k == TK_STAR || k == TK_SLASH ||
           k == TK_PERCENT || k == TK_AMP || k == TK_PIPE || k == TK_CARET ||
           k == TK_SHL || k == TK_SHR;
}

/* does the expression contain a call (so re-evaluating it could double a side
 * effect)? */
static int expr_has_call(Expr *e) {
    if (!e) return 0;
    if (e->kind == E_CALL) return 1;
    if (expr_has_call(e->lhs) || expr_has_call(e->rhs)) return 1;
    for (int i = 0; i < e->nargs; i++)
        if (expr_has_call(e->args[i])) return 1;
    return 0;
}

/* True if `e` contains an `or_return` (E_ORRETURN), which early-returns from the
 * enclosing function on the Err/None path. Such an expression CANNOT be wrapped in
 * a per-statement `_t` arena (MM-10): the early return would jump past the
 * `arena_free(&_t)` (leak) and bypass the proper return-frees. Exclude it. */
static int expr_has_orreturn(Expr *e) {
    if (!e) return 0;
    if (e->kind == E_ORRETURN) return 1;
    if (expr_has_orreturn(e->lhs) || expr_has_orreturn(e->rhs)) return 1;
    for (int i = 0; i < e->nargs; i++)
        if (expr_has_orreturn(e->args[i])) return 1;
    return 0;
}

/* True if `e` reads the local variable `name` (an E_IDENT whose name matches).
 * Detects a self-referential shadowing decl `y := y + 2`: the typechecker
 * resolves the RHS `y` against the ENCLOSING binding (it computes the decl's
 * type before the new name is in scope), so codegen must read the enclosing
 * binding too -- but a naive `T h_y = (h_y + 2)` reads the new C local in its
 * own initializer (use-before-init UB). Matches only E_IDENT, never E_FIELD
 * names (`obj.y` stores "y" on the E_FIELD, not as a child ident). Every other
 * node stores its operands in lhs/rhs/args (E_SLICE: base/lo/hi; call-on-expr:
 * callee in lhs; tuple/array/map elems in args), so the generic walk covers
 * them. The one exception is E_LAMBDA: its body is lifted to g_laminfo[ival].
 * proc and its captures live there (params[0..ncap]), NOT as child exprs -- so
 * a self-referential shadow whose RHS captures `name` (`f := fn(x:int)->int:
 * x + f(x)`) reads the enclosing binding through the env build and must route
 * through the temp too. Mirrors compiler/tychoc0.ty's expr_refs (it checks the
 * lambda's capture-name list); keeps both compilers' codegen byte-identical. */
static int expr_refs_local(Expr *e, const char *name) {
    if (!e) return 0;
    if (e->kind == E_IDENT && e->sval && !strcmp(e->sval, name)) return 1;
    if (e->kind == E_LAMBDA) {     /* captures are on the lifted proc, not in lhs/rhs/args */
        LamInfo *li = &g_laminfo[e->ival];
        for (int i = 0; i < li->ncap; i++)
            if (!strcmp(li->proc->params[i].name, name)) return 1;
        return 0;
    }
    if (expr_refs_local(e->lhs, name) || expr_refs_local(e->rhs, name)) return 1;
    for (int i = 0; i < e->nargs; i++)
        if (expr_refs_local(e->args[i], name)) return 1;
    return 0;
}

/* A compound assignment `a[i] OP= e` evaluates the place TWICE (read, then
 * store), so a side-effecting index `i` would run twice. Bind each index in the
 * place that CONTAINS A CALL to a fresh temp (queued in g_pending, emitted just
 * before the assignment) and rewrite the place to use it, so the index runs
 * once. Pure indices are untouched, so the common `a[i] += e` is byte-identical. */
static void hoist_index_calls(Expr *place, int line) {
    Expr *chain[32]; int nc = 0;
    for (Expr *cur = place; cur; ) {
        if (cur->kind == E_INDEX) {
            if (nc >= 32) die_at(line, "assignment place too deeply nested (max 32 indices)");
            chain[nc++] = cur;
        }
        if (cur->kind == E_INDEX || cur->kind == E_FIELD || cur->kind == E_TUPIDX) cur = cur->lhs;
        else break;
    }
    for (int i = nc - 1; i >= 0; i--) {   /* innermost index (evaluated first) hoisted first */
        Expr *ix = chain[i];
        if (ix->rhs && expr_has_call(ix->rhs)) {
            if (g_npending >= 64) die_at(line, "too many hoisted index expressions in one statement (max 64)");
            Stmt *d = new_stmt(S_DECL, line);
            d->name = sfmt("_cx%d", g_forin_uid++);
            d->expr = ix->rhs;
            g_pending[g_npending++] = d;
            Expr *tv = new_expr(E_IDENT, line);
            tv->sval = d->name; tv->pkg = g_cur_pkg_prefix;
            ix->rhs = tv;
        }
    }
}

static Stmt *parse_stmt(Parser *ps) {
    Tok *t = cur(ps);

    /* `const NAME = <literal>` — a function-local immutable named literal, folded
     * at each use (contextual keyword, like `delete`; a variable named `const` is
     * unaffected since `const` is a keyword only when a name follows). */
    if (t->kind == TK_IDENT && !strcmp(t->text, "const") && peek(ps, 1)->kind == TK_IDENT) {
        ps->p++;                                  /* eat 'const' */
        Tok *nameT = eat(ps, TK_IDENT, "a constant name after 'const'");
        eat(ps, TK_EQ, "'=' after the constant name");
        Expr *lit = const_fold(parse_expr(ps), 0);   /* local: literals + int arithmetic, no sibling-const refs */
        if (!is_literal_expr(lit))
            die_at(lit->line, "const value must be a literal");
        eat(ps, TK_NEWLINE, "newline");
        Stmt *s = new_stmt(S_CONST, t->line);
        s->name = nameT->text; s->expr = lit;
        return s;
    }

    /* `delete m[k]` -> m = map_del(m, k) (B5.2). `delete` is contextual: it is a
     * keyword only when an identifier (the map variable) follows, so a variable
     * named `delete` elsewhere is unaffected. */
    if (t->kind == TK_IDENT && !strcmp(t->text, "delete") && peek(ps, 1)->kind == TK_IDENT) {
        ps->p++;                                  /* eat 'delete' */
        /* the map element to remove: a full place expression `PLACE[key]` (the map may be
         * a bare variable, a struct field, or a nested index/tuple element), parsed as one
         * postfix expr so `delete c.idx[k]` works, not just `delete m[k]`. */
        Expr *e = parse_postfix(ps);
        if (e->kind != E_INDEX) die_at(t->line, "`delete` removes a map element: `delete m[k]`");
        Expr *mref = e->lhs;                      /* the map place */
        Expr *key  = e->rhs;                      /* the key */
        Expr *call = new_expr(E_CALL, t->line);   /* PLACE = map_del(PLACE, key) */
        call->sval = "map_del"; call->pkg = g_cur_pkg_prefix;
        call->args = (Expr **)xmalloc(2 * sizeof(Expr *));
        call->args[0] = mref; call->args[1] = key; call->nargs = 2;
        if (mref->kind == E_IDENT) {              /* bare variable: S_ASSIGN (keeps the in-place map_del rewrite) */
            Stmt *s = new_stmt(S_ASSIGN, t->line);
            s->name = mref->sval; s->expr = call;
            return s;
        }
        if (mref->kind == E_FIELD || mref->kind == E_INDEX || mref->kind == E_TUPIDX) {
            Stmt *s = new_stmt(mref->kind == E_INDEX ? S_INDEXSET : S_FIELDSET, t->line);
            s->target = mref; s->expr = call;     /* assign the new map back to the place */
            return s;
        }
        die_at(t->line, "cannot `delete` from this expression");
    }

    if (t->kind == TK_RETURN) {
        ps->p++;
        if (at(ps, TK_IF) || at(ps, TK_MATCH)) {   /* `return if.../match...`: desugar each tail to its own return */
            Stmt *c = parse_value_ctrl(ps);
            ctrl_rewrite_tails(c, S_RETURN, NULL, NULL);
            return c;
        }
        Stmt *s = new_stmt(S_RETURN, t->line);
        if (!at(ps, TK_NEWLINE)) {
            Expr *first = parse_expr(ps);
            if (at(ps, TK_COMMA)) {       /* return a, b, ... builds a tuple */
                Expr *e = new_expr(E_TUPLE, t->line);
                int cap = 4; e->args = (Expr **)xrealloc(e->args, (size_t)cap * sizeof(Expr *));
                e->args[e->nargs++] = first;
                while (accept(ps, TK_COMMA)) {
                    if (e->nargs == cap) { cap *= 2; e->args = (Expr **)xrealloc(e->args, (size_t)cap * sizeof(Expr *)); }
                    e->args[e->nargs++] = parse_expr(ps);
                }
                if (e->nargs > 8) die_at(t->line, "a tuple has at most 8 elements");
                s->expr = e;
            } else {
                s->expr = first;
            }
        }
        eat(ps, TK_NEWLINE, "newline");
        return s;
    }
    if (t->kind == TK_BREAK || t->kind == TK_CONTINUE) {
        ps->p++;
        Stmt *s = new_stmt(t->kind == TK_BREAK ? S_BREAK : S_CONTINUE, t->line);
        eat(ps, TK_NEWLINE, "newline");
        return s;
    }
    if (t->kind == TK_SELECT) {          /* select over channels (CC-5): recv arms + default/closed */
        ps->p++;
        Stmt *s = new_stmt(S_SELECT, t->line);
        eat(ps, TK_COLON, "':' before the select arms");
        eat(ps, TK_NEWLINE, "newline");
        eat(ps, TK_INDENT, "indented select arms");
        int cap = 0;
        while (!at(ps, TK_DEDENT) && !at(ps, TK_EOF)) {
            if (accept(ps, TK_NEWLINE)) continue;
            Tok *an = eat(ps, TK_IDENT, "a select arm: `recv(ch, x):`, `default:`, or `closed:`");
            if (s->narms == cap) {
                cap = cap ? cap * 2 : 4;
                s->arms = (MatchArm *)xrealloc(s->arms, (size_t)cap * sizeof(MatchArm));
                s->sel_ch = (Expr **)xrealloc(s->sel_ch, (size_t)cap * sizeof(Expr *));
            }
            MatchArm *arm = &s->arms[s->narms];
            arm->variant = an->text; arm->nbinds = 0; arm->line = an->line;
            Expr *che = NULL;
            if (!strcmp(an->text, "recv")) {
                eat(ps, TK_LPAREN, "'(' after recv");
                che = parse_expr(ps);
                eat(ps, TK_COMMA, "',' between the channel and the binding");
                arm->binds[arm->nbinds++] = eat(ps, TK_IDENT, "a binding name")->text;
                eat(ps, TK_RPAREN, "')'");
            } else if (strcmp(an->text, "default") != 0 && strcmp(an->text, "closed") != 0) {
                die_at(an->line, "a select arm is `recv(ch, x):`, `default:`, or `closed:`");
            }
            s->sel_ch[s->narms] = che;
            s->narms++;
            eat(ps, TK_COLON, "':' after the arm");
            eat(ps, TK_NEWLINE, "newline");
            arm->body = parse_block(ps, &arm->nbody);
        }
        eat(ps, TK_DEDENT, "end of the select arms");
        if (s->narms == 0) die_at(t->line, "select needs at least one arm");
        return s;
    }
    if (t->kind == TK_MATCH) {
        ps->p++;
        return parse_match(ps, t->line, 0);   /* statement form: block arms */
    }
    if (t->kind == TK_IF) {
        ps->p++;
        return parse_if(ps, t->line);
    }
    if (t->kind == TK_PARALLEL) {        /* parallel for ...: chunked fan-out + reduction merge (CC-3) */
        ps->p++;
        if (!at(ps, TK_FOR)) die_at(t->line, "expected 'for' after 'parallel'");
        g_parallel_ctx = 1;              /* the TK_FOR handler consumes this for the directly-following for */
        Stmt *s = parse_stmt(ps);        /* parses the for (range form, or foreach: deferred for type-directed lowering) */
        if (s->kind != S_FORRANGE)
            die_at(t->line, "parallel supports 'for x in range(...)' and 'for x in collection' loops only");
        s->parallel = 1;
        return s;
    }
    if (t->kind == TK_FOR) {
        ps->p++;
        int par_here = g_parallel_ctx; g_parallel_ctx = 0;   /* only THIS for is parallel; nested fors below are not */
        /* counting form `for i in range(...)` or foreach `for x in COLL:` */
        if (at(ps, TK_IDENT) && peek(ps, 1)->kind == TK_IN) {
            Tok *var = eat(ps, TK_IDENT, "a loop variable");
            eat(ps, TK_IN, "'in'");
            if (at(ps, TK_IDENT) && !strcmp(cur(ps)->text, "range") && peek(ps, 1)->kind == TK_LPAREN) {
                ps->p++;                     /* consume 'range' */
                eat(ps, TK_LPAREN, "'('");
                Stmt *s = new_stmt(S_FORRANGE, t->line);
                s->name = var->text;
                Expr *a1 = parse_expr(ps);
                Expr *a2 = NULL, *a3 = NULL;
                if (accept(ps, TK_COMMA)) a2 = parse_expr(ps);
                if (a2 && accept(ps, TK_COMMA)) a3 = parse_expr(ps);
                eat(ps, TK_RPAREN, "')'");
                eat(ps, TK_COLON, "':' before the block");
                eat(ps, TK_NEWLINE, "newline");
                if (!a2) {                   /* range(n): 0 .. n */
                    Expr *zero = new_expr(E_INT, t->line); zero->ival = 0;
                    s->r_start = zero; s->r_stop = a1; s->r_step = NULL;
                } else {                     /* range(a, b[, step]) */
                    s->r_start = a1; s->r_stop = a2; s->r_step = a3;
                }
                s->body = parse_block(ps, &s->nbody);
                return s;
            }
            /* foreach over a collection (array or string):
             *   for x in COLL:        _fcN := COLL
             *       <body>      ==>    for _fiN in range(0, len(_fcN)):
             *                              x := _fcN[_fiN]
             *                              <body>
             * COLL is bound to a temp so it is evaluated EXACTLY once; the element
             * read reuses array/string indexing and its bounds-check elision. The
             * temp decl is queued (g_pending) so parse_block emits it before the loop. */
            Expr *coll = parse_expr(ps);
            eat(ps, TK_COLON, "':' before the block");
            eat(ps, TK_NEWLINE, "newline");
            int nbody = 0; Stmt **ubody = parse_block(ps, &nbody);
            if (par_here) {
                /* `parallel for x in EXPR`: defer the array-vs-channel choice to
                 * resolve_parfor (types are needed and unknown here). EXPR must
                 * name a variable — both an array and a channel source are scalar
                 * handles, so no eval-once temp is required and a channel cannot be
                 * aliased into a temp. Node carries: name=loop var, r_start=source
                 * ident, body=raw body, foreach=1; parallel=1 is set by the caller. */
                if (coll->kind != E_IDENT)
                    die_at(t->line, "parallel for over a collection or channel must name a variable (bind it first)");
                Stmt *fe = new_stmt(S_FORRANGE, t->line);
                fe->foreach = 1; fe->name = var->text;
                fe->r_start = coll; fe->r_stop = NULL; fe->r_step = NULL;
                fe->body = ubody; fe->nbody = nbody;
                return fe;
            }
            int uid = g_forin_uid++;
            char *cn = sfmt("_fc%d", uid), *iv = sfmt("_fi%d", uid);
            Stmt *tmp = new_stmt(S_DECL, t->line);   /* _fcN := COLL (the prelude) */
            tmp->name = cn; tmp->expr = coll;
            Expr *cref = new_expr(E_IDENT, t->line); cref->sval = cn; cref->pkg = g_cur_pkg_prefix;
            Expr *iref = new_expr(E_IDENT, t->line); iref->sval = iv; iref->pkg = g_cur_pkg_prefix;
            Expr *idx = new_expr(E_INDEX, t->line); idx->lhs = cref; idx->rhs = iref;
            Stmt *elem = new_stmt(S_DECL, t->line);  /* x := _fcN[_fiN] */
            elem->name = var->text; elem->expr = idx;
            Stmt *fr = new_stmt(S_FORRANGE, t->line);
            fr->name = iv;
            Expr *zero = new_expr(E_INT, t->line); zero->ival = 0;
            Expr *cref2 = new_expr(E_IDENT, t->line); cref2->sval = cn; cref2->pkg = g_cur_pkg_prefix;
            Expr *lenc = new_expr(E_CALL, t->line); lenc->sval = "len"; lenc->pkg = g_cur_pkg_prefix;
            lenc->args = (Expr **)xmalloc(sizeof(Expr *)); lenc->args[0] = cref2; lenc->nargs = 1;
            fr->r_start = zero; fr->r_stop = lenc; fr->r_step = NULL;
            Stmt **fbody = (Stmt **)xmalloc((size_t)(nbody + 1) * sizeof(Stmt *));
            fbody[0] = elem;
            for (int k = 0; k < nbody; k++) fbody[k + 1] = ubody[k];
            fr->body = fbody; fr->nbody = nbody + 1;
            if (g_npending >= 64) die_at(t->line, "too many nested foreach loops in one block");
            g_pending[g_npending++] = tmp;
            return fr;
        }
        /* condition form: `for cond:` — does everything a while loop does */
        Stmt *s = new_stmt(S_WHILE, t->line);
        s->expr = parse_expr(ps);
        eat(ps, TK_COLON, "':' before the block");
        eat(ps, TK_NEWLINE, "newline");
        s->body = parse_block(ps, &s->nbody);
        return s;
    }

    /* destructuring `a, b := f()` (decl, new vars) or `a, b = f()` (assign to
     * existing vars) — an identifier immediately followed by a comma. The RHS
     * must yield a tuple. */
    if (t->kind == TK_IDENT && peek(ps, 1)->kind == TK_COMMA) {
        Stmt *s = new_stmt(S_MDECL, t->line);
        s->names[s->nnames++] = eat(ps, TK_IDENT, "a name")->text;
        while (accept(ps, TK_COMMA)) {
            if (s->nnames >= 8) die_at(t->line, "at most 8 destructuring targets");
            s->names[s->nnames++] = eat(ps, TK_IDENT, "a name in the destructuring list")->text;
        }
        if (!accept(ps, TK_COLONEQ)) {   /* `=` -> reassign existing vars */
            eat(ps, TK_EQ, "':=' (new vars) or '=' (existing vars)");
            s->kind = S_MASSIGN;
        }
        s->expr = parse_expr(ps);
        eat(ps, TK_NEWLINE, "newline");
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
            if (at(ps, TK_IF) || at(ps, TK_MATCH)) {   /* `x := if.../match...`: infer type + assign in resolve */
                s->ctrl = parse_value_ctrl(ps);
                return s;
            }
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
            if (at(ps, TK_IF) || at(ps, TK_MATCH)) {   /* `x : T = if.../match...` */
                s->ctrl = parse_value_ctrl(ps);
                return s;
            }
            s->expr = parse_expr(ps);
            eat(ps, TK_NEWLINE, "newline");
            return s;
        }
        eat(ps, TK_EQ, "'='");
        if (at(ps, TK_IF) || at(ps, TK_MATCH)) {   /* `x = if.../match...`: desugar each tail to `x = tail` */
            Stmt *c = parse_value_ctrl(ps);
            ctrl_rewrite_tails(c, S_ASSIGN, name, NULL);
            return c;
        }
        Stmt *s = new_stmt(S_ASSIGN, t->line);
        s->name = name;
        s->expr = parse_expr(ps);
        eat(ps, TK_NEWLINE, "newline");
        return s;
    }

    /* a place (var / index / field) being assigned, a compound assignment, or a
     * bare expression statement (a call). parse_postfix stops before any binary
     * operator, so `a[i] += v` leaves the `+` for the compound check below. */
    Expr *e = parse_postfix(ps);
    if (accept(ps, TK_EQ)) {
        if (e->kind != E_INDEX && e->kind != E_FIELD && e->kind != E_TUPIDX)
            die_at(t->line, "cannot assign to this expression");
        StmtKind sk = e->kind == E_INDEX ? S_INDEXSET : S_FIELDSET;
        if (at(ps, TK_IF) || at(ps, TK_MATCH)) {   /* `place = if.../match...`: desugar each tail to `place = tail` */
            Stmt *c = parse_value_ctrl(ps);
            ctrl_rewrite_tails(c, sk, NULL, e);
            return c;
        }
        Stmt *s = new_stmt(sk, t->line);
        s->target = e;
        s->expr = parse_expr(ps);
        eat(ps, TK_NEWLINE, "newline");
        return s;
    }
    /* compound assignment `target OP= rhs` -> `target = target OP rhs`. The
     * variable form `x += e` lands here too (the plain `x = e` form is taken by
     * the identifier branch above). For an index/field target the lvalue is
     * evaluated twice in the generated C (read then store) — sound when the
     * index expression has no side effects, exactly like writing it out longhand. */
    if (is_compound_op(cur(ps)->kind) && peek(ps, 1)->kind == TK_EQ) {
        TokKind op = cur(ps)->kind; ps->p++;
        eat(ps, TK_EQ, "'=' to complete the compound assignment");
        Expr *rhs = parse_expr(ps);
        eat(ps, TK_NEWLINE, "newline");
        if (e->kind == E_INDEX || e->kind == E_FIELD || e->kind == E_TUPIDX)
            hoist_index_calls(e, t->line);   /* single-eval a side-effecting index */
        Expr *b = new_expr(E_BINOP, t->line);
        b->op = op; b->lhs = e; b->rhs = rhs;
        if (e->kind == E_IDENT) {
            Stmt *s = new_stmt(S_ASSIGN, t->line);
            s->name = e->sval; s->expr = b;
            return s;
        }
        if (e->kind == E_INDEX || e->kind == E_FIELD || e->kind == E_TUPIDX) {
            Stmt *s = new_stmt(e->kind == E_INDEX ? S_INDEXSET : S_FIELDSET, t->line);
            s->target = e; s->expr = b;
            return s;
        }
        die_at(t->line, "cannot compound-assign to this expression");
    }
    /* The only bare expression statement is a call -- it can have side effects.
     * A bare identifier / index / field / `or_return` has no effect and is almost
     * always an incomplete statement (e.g. a truncated `p.x = ...`). Reject it,
     * matching tychoc0, whose statement grammar only accepts `name(args)` here. */
    if (e->kind != E_CALL)
        die_at(t->line, "a statement must be a declaration, assignment, or call -- a bare expression has no effect");
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
        Stmt *st = parse_stmt(ps);
        /* a foreach queued a collection-temp decl to emit before its loop */
        for (int k = 0; k < g_npending; k++) {
            if (n == cap) { cap = cap ? cap * 2 : 8; body = (Stmt **)xrealloc(body, (size_t)cap * sizeof(Stmt *)); }
            body[n++] = g_pending[k];
        }
        g_npending = 0;
        if (n == cap) { cap = cap ? cap * 2 : 8; body = (Stmt **)xrealloc(body, (size_t)cap * sizeof(Stmt *)); }
        body[n++] = st;
    }
    eat(ps, TK_DEDENT, "dedent");
    *count = n;
    return body;
}

static Proc *parse_fn(Parser *ps) {
    g_ncur_typarams = 0;                  /* fresh `$T` scope for this function */
    eat(ps, TK_FN, "'fn'");
    Tok *nameT = eat(ps, TK_IDENT, "a procedure name");
    eat(ps, TK_LPAREN, "'('");

    Proc *pr = (Proc *)xmalloc(sizeof(Proc));
    memset(pr, 0, sizeof *pr);
    pr->name = pkg_mangle(nameT->text);
    pr->line = nameT->line;

    int cap = 0;
    while (!at(ps, TK_RPAREN)) {
        Tok *pn = eat(ps, TK_IDENT, "a parameter name");
        eat(ps, TK_COLON, "':' after parameter name");
        int is_inout = accept(ps, TK_INOUT);   /* `name: mut type` */
        /* PROTOTYPE: `name: sink type` — an owned, mutable parameter. Contextual
         * keyword (not reserved). The callee owns the argument and may mutate it
         * in place; the caller's argument is consumed (adopted if it is a movable
         * dead local / fresh value, else copied — see arg_into at the call site). */
        int is_sink = 0;
        if (!is_inout && at(ps, TK_IDENT) && !strcmp(cur(ps)->text, "sink")) { ps->p++; is_sink = 1; }
        Type pt = parse_type(ps);
        if (pr->nparams == cap) { cap = cap ? cap * 2 : 4; pr->params = (Param *)xrealloc(pr->params, (size_t)cap * sizeof(Param)); }
        pr->params[pr->nparams].name = pn->text;
        pr->params[pr->nparams].type = pt;
        pr->params[pr->nparams].is_inout = is_inout;
        pr->params[pr->nparams].is_sink = is_sink;
        pr->params[pr->nparams].ffi_ct = NULL;
        pr->nparams++;
        if (!accept(ps, TK_COMMA)) break;
    }
    eat(ps, TK_RPAREN, "')'");

    if (accept(ps, TK_ARROW)) { pr->ret = parse_type(ps); pr->has_ret = 1; }
    else pr->ret = T_VOID;
    if (IS_HANDLE(pr->ret))   /* FFI R2: a handle is freed at its owner's scope exit; returning it (the only way is from an extern opener) would free-then-escape */
        die_at(pr->line, "a Tycho fn cannot return a handle -- only an `extern fn` opener may; a handle is freed at the end of its scope and cannot escape it");
    pr->generic = (g_ncur_typarams > 0);   /* a `$T` in the signature makes this a template */
    pr->ntyparams = g_ncur_typarams;       /* record the $-params in order, for explicit call-site type args */
    for (int i = 0; i < g_ncur_typarams; i++) pr->typarams[i] = typaram_of(g_cur_typarams[i]);

    /* generics: optional `where pred(T), pred2(T2)` -- a fixed compiler-known
     * predicate set, checked at instantiation against the inferred concrete type. */
    if (cur(ps)->kind == TK_IDENT && !strcmp(cur(ps)->text, "where")) {
        if (!pr->generic) die_at(cur(ps)->line, "`where` constraints require a generic function (one with a `$T` parameter)");
        ps->p++;
        for (;;) {
            if (pr->ncon >= 8) die_at(cur(ps)->line, "at most 8 `where` constraints per function");
            Tok *pt = eat(ps, TK_IDENT, "a `where` predicate (numeric/comparable/has_str) or a type parameter");
            if (at(ps, TK_COLON)) {   /* type-set form: T: type1 | type2 | ...  (Go-style) */
                int known = 0;
                for (int i = 0; i < g_ncur_typarams; i++) if (!strcmp(g_cur_typarams[i], pt->text)) known = 1;
                if (!known) die_at(pt->line, "`where %s: ...`: '%s' is not a type parameter of this function", pt->text, pt->text);
                ps->p++;   /* the ':' */
                pr->con_pred[pr->ncon] = NULL;
                pr->con_tp[pr->ncon]   = typaram_of(pt->text);
                int n = 0;
                pr->con_set[pr->ncon][n++] = parse_type(ps);
                while (accept(ps, TK_PIPE)) {
                    if (n >= 16) die_at(pt->line, "at most 16 types in a `where` type set");
                    pr->con_set[pr->ncon][n++] = parse_type(ps);
                }
                pr->con_nset[pr->ncon] = n;
            } else {                  /* predicate form: pred(T) */
                if (strcmp(pt->text, "numeric") && strcmp(pt->text, "comparable") && strcmp(pt->text, "has_str"))
                    die_at(pt->line, "unknown `where` predicate '%s' (known: numeric, comparable, has_str -- or use a type set, `T: int | float`)", pt->text);
                eat(ps, TK_LPAREN, "'(' after a `where` predicate");
                Tok *tn = eat(ps, TK_IDENT, "a type-parameter name");
                int known = 0;
                for (int i = 0; i < g_ncur_typarams; i++) if (!strcmp(g_cur_typarams[i], tn->text)) known = 1;
                if (!known) die_at(tn->line, "`where` refers to '%s', which is not a type parameter of this function", tn->text);
                eat(ps, TK_RPAREN, "')'");
                pr->con_pred[pr->ncon] = pt->text;
                pr->con_tp[pr->ncon]   = typaram_of(tn->text);
                pr->con_nset[pr->ncon] = 0;
            }
            pr->ncon++;
            if (!accept(ps, TK_COMMA)) break;
        }
    }

    eat(ps, TK_COLON, "':' before the block");
    eat(ps, TK_NEWLINE, "newline");
    pr->body = parse_block(ps, &pr->nbody);   /* type params stay in scope for the body */
    g_ncur_typarams = 0;                  /* leave the function's `$T` scope */
    return pr;
}

/* FFI Stage 1: only scalars + string may cross the C boundary. Composite types
 * (arrays/maps/structs/Option/Result/tuples/fn) have tycho-internal C reps, not a
 * stable C ABI — reject them (fail closed). void is allowed as a return only. */
static int ffi_scalar_type(Type t) {
    return t == T_INT || t == T_CHAR || t == T_FLOAT || t == T_BOOL || t == T_STRING || t == T_PTR ||
           t == T_U32 || t == T_U64 || t == T_F32;   /* first-class sized numerics cross as their real C type */
}

/* FFI boundary-only sized-integer types (u8/u16/i8/i16/i32/i64): recognized ONLY in
 * `extern fn` param/return positions (never a general Tycho type — `int` to Tycho, no
 * leak into arithmetic/printing). This string is the real C ABI type emitted in the
 * extern prototype so a call matches e.g. `int16_t f(uint8_t)`. (u32/u64/f32 are their
 * own first-class types — a real u32 already emits `unsigned int` via c_type — so they
 * are handled by parse_type, not here.) Built-in C types, ABI-compatible with the
 * uintN_t typedefs on every supported target (LP64 Linux/macOS, LLP64 Windows). */
static const char *ffi_sized_ctype(const char *n) {
    if (!strcmp(n, "u8"))  return "unsigned char ";
    if (!strcmp(n, "u16")) return "unsigned short ";
    if (!strcmp(n, "i8"))  return "signed char ";
    if (!strcmp(n, "i16")) return "short ";
    if (!strcmp(n, "i32")) return "int ";
    if (!strcmp(n, "i64")) return "long long ";
    return NULL;
}

/* extern [ "Lib" ] fn name(p: T, ...) [-> T]    (bodyless; calls a C symbol).
 * The name is NOT pkg_mangled — a C symbol is global. */
static Proc *parse_extern_fn(Parser *ps) {
    ps->p++;   /* consume the `extern` ident (caller verified its text) */
    const char *lib = NULL;
    if (at(ps, TK_STR)) { lib = cur(ps)->text; ps->p++; }   /* optional link-library name */
    eat(ps, TK_FN, "'fn' after 'extern'");
    Tok *nameT = eat(ps, TK_IDENT, "a C function name");
    eat(ps, TK_LPAREN, "'('");

    Proc *pr = (Proc *)xmalloc(sizeof(Proc));
    memset(pr, 0, sizeof *pr);
    pr->name = nameT->text;          /* literal C symbol — never mangled */
    pr->line = nameT->line;
    pr->is_extern = 1;
    pr->lib = lib;

    int cap = 0;
    while (!at(ps, TK_RPAREN)) {
        Tok *pn = eat(ps, TK_IDENT, "a parameter name");
        eat(ps, TK_COLON, "':' after parameter name");
        int p_inout = accept(ps, TK_INOUT);   /* FFI R4: `name: mut T` = an out / in-out param — the C fn writes through a T* */
        /* FFI-boundary sized int (u8/u16/.../i64): `int` to Tycho, sized C type at the ABI.
         * Only for a by-value param (a sized `mut` out-param isn't supported yet). */
        const char *p_ffi_ct = NULL;
        Type pt;
        if (!p_inout && at(ps, TK_IDENT) && (p_ffi_ct = ffi_sized_ctype(cur(ps)->text)) != NULL) {
            ps->p++;          /* consume the sized-type name (an ordinary ident elsewhere) */
            pt = T_INT;
        } else {
            pt = parse_type(ps);
        }
        if (p_inout) {
            /* An out-param's address must be a clean T* the C side fills and tycho reads
             * back by value: ptr (the `T**` constructor shape) plus the numeric scalars
             * qualify. string is a length-headered char* (a char** out-param would hand
             * tycho a raw C pointer with no length header) — banned, as are bytes/handle/
             * composite, which have no trivial pointer-to-self ABI. */
            if (!ffi_scalar_type(pt) || pt == T_STRING) die_at(pn->line, "extern fn '%s': a `mut` (out) parameter '%s' must be int/char/float/bool/ptr — string/bytes/handle/composite have no trivial out-param ABI", pr->name, pn->text);
        } else if (!ffi_scalar_type(pt) && pt != T_BYTES && !IS_HANDLE(pt)) {
            die_at(pn->line, "extern fn '%s': parameter '%s' must be int/char/float/bool/string/ptr/bytes or a handle (no composites across the C boundary)", pr->name, pn->text);
        }
        if (pr->nparams == cap) { cap = cap ? cap * 2 : 4; pr->params = (Param *)xrealloc(pr->params, (size_t)cap * sizeof(Param)); }
        pr->params[pr->nparams].name = pn->text;
        pr->params[pr->nparams].type = pt;
        pr->params[pr->nparams].is_inout = p_inout;
        pr->params[pr->nparams].is_sink = 0;   /* extern params are never sink */
        pr->params[pr->nparams].ffi_ct = p_ffi_ct;
        pr->nparams++;
        if (!accept(ps, TK_COMMA)) break;
    }
    eat(ps, TK_RPAREN, "')'");

    if (accept(ps, TK_ARROW)) {
        /* FFI-boundary sized int return: `int` to Tycho, sized C type at the ABI. */
        if (at(ps, TK_IDENT) && (pr->ret_ffi_ct = ffi_sized_ctype(cur(ps)->text)) != NULL) {
            ps->p++; pr->ret = T_INT;
        } else {
            pr->ret = parse_type(ps);
        }
        pr->has_ret = 1;
        /* FFI R3a: `-> Option(string)` is allowed — a C NULL return surfaces as None
         * (vs `-> string` which maps NULL to ""), so a nullable C getter need not use
         * a sentinel. The C symbol still returns char*; the wrapper does the NULL test. */
        int ret_opt_str = (IS_OPT(pr->ret) && opt_inner(pr->ret) == T_STRING);
        if (!ffi_scalar_type(pr->ret) && pr->ret != T_BYTES && !IS_HANDLE(pr->ret) && !ret_opt_str) die_at(pr->line, "extern fn '%s': return type must be int/char/float/bool/string/ptr/bytes, Option(string), a handle, or omitted", pr->name);
    } else {
        pr->ret = T_VOID;
    }
    eat(ps, TK_NEWLINE, "newline (an extern fn has no body)");
    return pr;
}

/* struct Name:
 *     field: type
 *     ...
 * Registered into g_structs immediately so later declarations can name it
 * as a type (a struct must be defined before it is used as a type). */
/* handle Name:
 *     free: c_free_fn
 * Declares an affine, opaque (void*) C handle whose destructor `c_free_fn` (a C
 * symbol, normally an `extern fn c_free_fn(h: Name)`) the compiler calls at the
 * owning variable's scope exit. See docs/internals/typed-handles-design.md. */
static void parse_handle(Parser *ps) {
    eat(ps, TK_HANDLE, "'handle'");
    Tok *nameT = eat(ps, TK_IDENT, "a handle name");
    const char *nm = pkg_mangle(nameT->text);
    if (struct_find(nm) >= 0 || enum_find(nm) >= 0 || newtype_find(nm) >= 0 || handle_find(nm) >= 0)
        die_at(nameT->line, "'%s' is already defined", nameT->text);
    if (g_nhandles >= 256) die_at(nameT->line, "too many handle types (max 256)");
    eat(ps, TK_COLON, "':' before the handle body");
    eat(ps, TK_NEWLINE, "newline");
    eat(ps, TK_INDENT, "an indented 'free: <c_free_fn>' line");
    Tok *kw = eat(ps, TK_IDENT, "'free'");
    if (strcmp(kw->text, "free")) die_at(kw->line, "a handle body is exactly 'free: <c_free_fn>'");
    eat(ps, TK_COLON, "':' after 'free'");
    Tok *fn = eat(ps, TK_IDENT, "the C destructor function name");
    eat(ps, TK_NEWLINE, "newline");
    eat(ps, TK_DEDENT, "dedent");
    g_handles[g_nhandles].name = nm;
    g_handles[g_nhandles].free_fn = fn->text;   /* a C symbol; emitted as free_fn(h) at scope exit */
    g_handles[g_nhandles].line = nameT->line;
    g_nhandles++;
}

static void parse_struct(Parser *ps) {
    eat(ps, TK_STRUCT, "'struct'");
    Tok *nameT = eat(ps, TK_IDENT, "a struct name");
    g_ncur_typarams = 0;                         /* generics: fresh `$T` scope for this struct */
    Type _tp[8]; int _ntp = 0;                   /* `struct Box($T, $U)` type parameters */
    if (accept(ps, TK_LPAREN)) {
        while (!at(ps, TK_RPAREN)) {
            Type tp = parse_type(ps);            /* `$T` registers the name + returns its typaram type; a bare field `T` then refers to it */
            if (!IS_TYPARAM(tp)) die_at(nameT->line, "a struct type parameter must be written `$Name`");
            if (_ntp >= 8) die_at(nameT->line, "too many struct type parameters (max 8)");
            _tp[_ntp++] = tp;
            if (!accept(ps, TK_COMMA)) break;
        }
        eat(ps, TK_RPAREN, "')' after the struct type parameters");
    }
    /* check the MANGLED name (like the enum site): a cross-package collision
     * ("a__b" + "c" vs "a" + "b__c") otherwise slips through to a duplicate C
     * typedef and fails at cc with no tycho-level diagnostic. */
    if (struct_find(pkg_mangle(nameT->text)) >= 0 || enum_find(pkg_mangle(nameT->text)) >= 0 || newtype_find(pkg_mangle(nameT->text)) >= 0)
        die_at(nameT->line, "'%s' is already defined", nameT->text);
    if (g_nstructs >= T_ARRC_BASE - T_STRUCT_BASE) die_at(nameT->line, "too many structs");
    TBL_ENSURE(g_structs, g_nstructs, g_structs_cap);
    eat(ps, TK_COLON, "':' before the block");
    eat(ps, TK_NEWLINE, "newline");
    eat(ps, TK_INDENT, "an indented field list");

    StructDef *sd = &g_structs[g_nstructs];
    sd->name = pkg_mangle(nameT->text);
    sd->fields = NULL; sd->nfields = 0; sd->fields_cap = 0;
    sd->line = nameT->line;
    sd->generic = (_ntp > 0); sd->ntyparams = _ntp;   /* generics: a template; instances substitute $T */
    for (int i = 0; i < _ntp; i++) sd->typarams[i] = _tp[i];
    sd->from_tmpl = -1; sd->nfrom_args = 0;            /* not an instance */
    g_nstructs++;   /* register the name BEFORE parsing fields, so a field type
                     * may reference this struct — e.g. a recursive `[Node]`
                     * child list. (Parsing is single-pass and sequential, so a
                     * half-built struct is only visible to its own fields.) */
    while (!at(ps, TK_DEDENT) && !at(ps, TK_EOF)) {
        if (accept(ps, TK_NEWLINE)) continue;
        Tok *fn = eat(ps, TK_IDENT, "a field name");
        for (int fi = 0; fi < sd->nfields; fi++)   /* fail-closed: a dup field name would emit a duplicate C member */
            if (!strcmp(sd->fields[fi].name, fn->text))
                die_at(fn->line, "duplicate field '%s'", fn->text);
        eat(ps, TK_COLON, "':' after field name");
        Type ft = parse_type(ps);   /* int, string, a struct, [Struct]/[[T]], Option(T), ... */
        TBL_ENSURE(sd->fields, sd->nfields, sd->fields_cap);
        sd->fields[sd->nfields].name = fn->text;
        sd->fields[sd->nfields].type = ft;
        sd->nfields++;
        eat(ps, TK_NEWLINE, "newline");
    }
    eat(ps, TK_DEDENT, "dedent");
    if (sd->nfields == 0) die_at(nameT->line, "a struct needs at least one field");
    g_ncur_typarams = 0;                         /* generics: leave the struct's `$T` scope */
}

static void parse_enum(Parser *ps) {
    eat(ps, TK_ENUM, "'enum'");
    Tok *nameT = eat(ps, TK_IDENT, "an enum name");
    g_ncur_typarams = 0;                         /* generics: fresh `$T` scope for this enum */
    Type _tp[8]; int _ntp = 0;                   /* `enum Tree($T, $U)` type parameters */
    if (accept(ps, TK_LPAREN)) {
        while (!at(ps, TK_RPAREN)) {
            Type tp = parse_type(ps);            /* `$T` registers the name + returns its typaram type; a bare payload `T` then refers to it */
            if (!IS_TYPARAM(tp)) die_at(nameT->line, "an enum type parameter must be written `$Name`");
            if (_ntp >= 8) die_at(nameT->line, "too many enum type parameters (max 8)");
            _tp[_ntp++] = tp;
            if (!accept(ps, TK_COMMA)) break;
        }
        eat(ps, TK_RPAREN, "')' after the enum type parameters");
    }
    if (struct_find(pkg_mangle(nameT->text)) >= 0 || enum_find(pkg_mangle(nameT->text)) >= 0 || newtype_find(pkg_mangle(nameT->text)) >= 0)
        die_at(nameT->line, "'%s' is already defined", nameT->text);
    if (g_nenums >= T_TUP_BASE - T_ENUM_BASE) die_at(nameT->line, "too many enums");
    TBL_ENSURE(g_enums, g_nenums, g_enums_cap);
    eat(ps, TK_COLON, "':' before the variants");
    eat(ps, TK_NEWLINE, "newline");
    eat(ps, TK_INDENT, "an indented variant list");
    EnumDef *ed = &g_enums[g_nenums];
    ed->name = pkg_mangle(nameT->text);
    ed->variants = NULL; ed->nvariants = 0; ed->variants_cap = 0;
    ed->line = nameT->line;
    ed->generic = (_ntp > 0); ed->ntyparams = _ntp;   /* generics: a template; instances substitute $T in payloads */
    for (int i = 0; i < _ntp; i++) ed->typarams[i] = _tp[i];
    ed->from_tmpl = -1; ed->nfrom_args = 0;            /* not an instance */
    g_nenums++;   /* register early so a variant payload can be this enum (recursion) */
    while (!at(ps, TK_DEDENT) && !at(ps, TK_EOF)) {
        if (accept(ps, TK_NEWLINE)) continue;
        Tok *vn = eat(ps, TK_IDENT, "a variant name");
        TBL_ENSURE(ed->variants, ed->nvariants, ed->variants_cap);
        char *vmn = pkg_mangle(vn->text);   /* variant names are package-scoped (mangled with the enum's package) */
        int dup;
        if (variant_find(vmn, &dup) >= 0)
            die_at(vn->line, "variant name '%s' is already used in this package", vn->text);
        Variant *var = &ed->variants[ed->nvariants];
        var->name = vmn;
        var->npayload = 0;
        if (accept(ps, TK_LPAREN)) {     /* a payload tuple, e.g. Add(Expr, Expr) */
            while (!at(ps, TK_RPAREN)) {
                if (var->npayload >= 8) die_at(vn->line, "too many payload fields (max 8)");
                var->payload[var->npayload++] = parse_type(ps);
                if (!accept(ps, TK_COMMA)) break;
            }
            eat(ps, TK_RPAREN, "')'");
        }
        ed->nvariants++;
        eat(ps, TK_NEWLINE, "newline");
    }
    eat(ps, TK_DEDENT, "dedent");
    if (ed->nvariants == 0) die_at(nameT->line, "an enum needs at least one variant");
    g_ncur_typarams = 0;                         /* generics: leave the enum's `$T` scope */
}

/* `type X = int` / `type X = float` — a distinct, zero-cost newtype. */
static void parse_typedecl(Parser *ps) {
    eat(ps, TK_TYPE, "'type'");
    Tok *nameT = eat(ps, TK_IDENT, "a type name");
    if (struct_find(pkg_mangle(nameT->text)) >= 0 || enum_find(pkg_mangle(nameT->text)) >= 0 || newtype_find(pkg_mangle(nameT->text)) >= 0)
        die_at(nameT->line, "'%s' is already defined", nameT->text);
    if (g_nnewtypes >= T_SOA_BASE - T_NT_BASE) die_at(nameT->line, "too many newtypes");
    TBL_ENSURE(g_newtypes, g_nnewtypes, g_newtypes_cap);
    eat(ps, TK_EQ, "'=' in a type declaration");
    Type under = parse_type(ps);
    if (under != T_INT && under != T_FLOAT && under != T_STRING && under != T_BOOL
        && !is_array(under) && !is_map(under) && !IS_STRUCT(under))
        die_at(nameT->line, "a newtype's underlying type must be int, float, string, bool, an array, a map, or a struct (got %s)", type_name(under));
    eat(ps, TK_NEWLINE, "newline");
    g_newtypes[g_nnewtypes].name = pkg_mangle(nameT->text);
    g_newtypes[g_nnewtypes].under = under;
    g_nnewtypes++;
}

/* ------------------------------------------------ package/import headers
 * `package`/`import` are contextual: they are only special as the leading
 * identifier of a top-level item, so they remain ordinary identifiers
 * everywhere else (no reserved words added). Stage A parses them and records
 * the package name + imports; imports are not yet resolved (Stage B). */
static const char *g_parsed_package = NULL;   /* package of the file just parsed (NULL = none) */
typedef struct { const char *alias; const char *path; int line; } Import;
static Import *g_imports;
static int    g_imports_cap = 0;
static int    g_nimports = 0;

static void parse_package_decl(Parser *ps) {
    ps->p++;                                    /* consume the `package` identifier */
    Tok *name = eat(ps, TK_IDENT, "a package name after `package`");
    g_parsed_package = name->text;
    accept(ps, TK_NEWLINE);
}

static void parse_import_decl(Parser *ps) {
    Tok *kw = cur(ps);
    ps->p++;                                    /* consume the `import` identifier */
    const char *alias = NULL;
    if (at(ps, TK_IDENT)) { alias = cur(ps)->text; ps->p++; }   /* optional alias */
    Tok *path = eat(ps, TK_STR, "an import path string");
    TBL_ENSURE(g_imports, g_nimports, g_imports_cap);
    g_imports[g_nimports].alias = alias;
    g_imports[g_nimports].path  = path->text;
    g_imports[g_nimports].line  = kw->line;
    g_nimports++;
    accept(ps, TK_NEWLINE);
}

/* Map a source qualifier (`geom` in `geom.add`) to its package prefix. An
 * aliased import (`import g "math/geom"`) binds the alias to the package's real
 * name (the path's last component); a plain `import "geom"` binds the name
 * itself. Unknown qualifiers fall through to `<qualifier>__` and fail loudly at
 * lookup if no such package was imported. */
/* corelib collection root: an import path "core:strings" resolves to
 * $TYCHO_CORELIB/strings (a library importable from any program, independent of the
 * importer's location), and binds the name `strings`. Other paths stay relative to
 * the importing package's directory. */
static const char *pkg_basename(const char *p) {
    if (!strncmp(p, "core:", 5)) p += 5;        /* "core:text/utf8" -> "utf8" */
    const char *slash = strrchr(p, '/');
    return slash ? slash + 1 : p;
}
/* argv[0] of this process, captured in main(); a fallback for locating the
 * binary when /proc/self/exe is unavailable. */
static const char *g_argv0 = NULL;

static int dir_exists(const char *p) {
    DIR *d = opendir(p);
    if (d) { closedir(d); return 1; }
    return 0;
}

/* Directory containing the running tychoc binary. Tries /proc/self/exe (Linux),
 * then argv[0]. Returns "." when no directory component is known. Computed once. */
static const char *exe_dir(void) {
    static char dirbuf[PATH_MAX];
    static int computed = 0;
    if (computed) return dirbuf[0] ? dirbuf : NULL;
    computed = 1;
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n > 0) buf[n] = '\0';
    else if (g_argv0) { strncpy(buf, g_argv0, sizeof buf - 1); buf[sizeof buf - 1] = '\0'; }
    else { dirbuf[0] = '\0'; return NULL; }
    char *slash = strrchr(buf, '/');
    if (!slash) strcpy(dirbuf, ".");
    else { *slash = '\0'; strcpy(dirbuf, buf[0] ? buf : "/"); }
    return dirbuf;
}

/* The corelib search root. TYCHO_CORELIB overrides; otherwise look next to the
 * tychoc binary (so `./tychoc prog.ty` finds `./corelib` with no setup), then in
 * an installed `share/tycho/corelib` layout. Returns NULL if none is found. */
static const char *corelib_root(void) {
    static int done = 0;
    static const char *cached = NULL;
    if (done) return cached;
    done = 1;
    const char *env = getenv("TYCHO_CORELIB");
    if (env && *env) return (cached = env);
    const char *ed = exe_dir();
    if (ed) {
        char *c1 = sfmt("%s/corelib", ed);
        if (dir_exists(c1)) return (cached = c1);
        char *c2 = sfmt("%s/../share/tycho/corelib", ed);
        if (dir_exists(c2)) return (cached = c2);
    }
    return (cached = NULL);
}

static char *resolve_pkg_dir(const char *importer_dir, const char *path) {
    if (!strncmp(path, "core:", 5)) {
        const char *root = corelib_root();
        if (!root) {
            const char *ed = exe_dir();
            fprintf(stderr,
                "tychoc: cannot find the corelib for import \"%s\".\n"
                "  Looked next to the tychoc binary (%s/corelib) and in %s/../share/tycho/corelib.\n"
                "  Set TYCHO_CORELIB to the corelib directory to override.\n",
                path, ed ? ed : "?", ed ? ed : "?");
            exit(1);
        }
        return sfmt("%s/%s", root, path + 5);
    }
    return sfmt("%s/%s", importer_dir, path);
}

static char *pkg_prefix_for(const char *qualifier) {
    const char *pkgname = qualifier;
    for (int i = 0; i < g_nimports; i++) {
        if (g_imports[i].alias && !strcmp(g_imports[i].alias, qualifier)) {
            pkgname = pkg_basename(g_imports[i].path);
            break;
        }
    }
    return sfmt("%s__", pkgname);
}

/* is `name` a package this file imported (by alias or by its path's last
 * component)? Used to read `pkg.Variant` as a qualified value, not a field. */
static int is_imported_pkg(const char *name) {
    for (int i = 0; i < g_nimports; i++) {
        if (g_imports[i].alias && !strcmp(g_imports[i].alias, name)) return 1;
        if (!strcmp(pkg_basename(g_imports[i].path), name)) return 1;
    }
    return 0;
}

/* Package privacy (B3): a top-level name with a leading underscore is private
 * to its own package. A qualified reference `qualifier.name` always names an
 * imported (hence foreign) package, so reject it when `name` starts with '_'. */
static void check_pkg_private(const char *qualifier, const char *name, int line) {
    if (name && name[0] == '_' && is_imported_pkg(qualifier))
        die_at(line, "'%s.%s' is package-private: a leading-underscore name "
               "is not accessible from another package", qualifier, name);
}

/* --------------------------------------------------- top-level constants
 * `const NAME = <literal>` is an immutable named literal folded at each use
 * (Tycho has no runtime globals). Top-level consts live here (persistent
 * across function bodies, so a use can forward-reference a later decl); local
 * consts ride g_vars via vars_push_const. Both fold in resolve_expr E_IDENT. */
typedef struct { char *name; Type type; Expr *lit; } ConstDef;
static ConstDef *g_consts;
static int g_nconsts = 0, g_consts_cap = 0;

static Type lit_type(Expr *lit) {
    switch (lit->kind) {
        case E_INT:   return T_INT;
        case E_CHAR:  return T_CHAR;
        case E_FLOAT: return T_FLOAT;
        case E_BOOL:  return T_BOOL;
        case E_STR:   return T_STRING;
        default:      return T_VOID;
    }
}
static int is_literal_expr(Expr *e) {
    return e->kind == E_INT || e->kind == E_CHAR || e->kind == E_FLOAT
        || e->kind == E_BOOL || e->kind == E_STR;
}
/* Fold a const-expression into a single literal at parse time. Handles:
 *   - unary `-`/`~` over an int literal, unary `-` over a float literal
 *     (so `const MIN = -100`, `const T = -3.14` collapse to one negative literal);
 *   - integer arithmetic/bitwise `+ - * / % & | ^ << >>` over int literals
 *     (so `const KB = 1024`, `const MB = 1024 * 1024`, `const MASK = 1 << 8`);
 *   - when `refs`, a bare identifier resolves to an earlier top-level const's
 *     literal (`const MB = KB * 1024`).
 * Float arithmetic and (with refs off) identifier refs are left unfolded and
 * then fail the is_literal_expr check at the call site — fail closed. */
static Expr *const_fold(Expr *e, int refs) {
    if (!e) return e;
    if (e->kind == E_IDENT) {
        if (refs) { Expr *c = consts_find(pkg_mangle(e->sval)); if (c) return c; }
        return e;
    }
    if (e->kind != E_BINOP) return e;
    if (e->rhs == NULL) {                        /* unary: operand in lhs */
        Expr *a = const_fold(e->lhs, refs);
        if (e->op == TK_MINUS) {
            if (a->kind == E_INT)   { Expr *n = new_expr(E_INT, e->line);   n->ival = -a->ival; return n; }
            if (a->kind == E_FLOAT) { Expr *n = new_expr(E_FLOAT, e->line); n->fval = -a->fval; return n; }
        } else if (e->op == TK_TILDE && e->lhs && (a->kind == E_INT)) {
            Expr *n = new_expr(E_INT, e->line);  n->ival = ~a->ival; return n;
        }
        return e;
    }
    Expr *a = const_fold(e->lhs, refs), *b = const_fold(e->rhs, refs);
    if (a->kind != E_INT || b->kind != E_INT) return e;   /* int-only const arithmetic */
    long x = a->ival, y = b->ival, r;
    switch (e->op) {
        case TK_PLUS:    r = x + y; break;
        case TK_MINUS:   r = x - y; break;
        case TK_STAR:    r = x * y; break;
        case TK_SLASH:   if (y == 0) die_at(e->line, "const expression divides by zero"); r = x / y; break;
        case TK_PERCENT: if (y == 0) die_at(e->line, "const expression divides by zero"); r = x % y; break;
        case TK_AMP:     r = x & y; break;
        case TK_PIPE:    r = x | y; break;
        case TK_CARET:   r = x ^ y; break;
        case TK_SHL:     r = x << y; break;
        case TK_SHR:     r = x >> y; break;
        default: return e;
    }
    Expr *n = new_expr(E_INT, e->line); n->ival = r; return n;
}
static Expr *consts_find(const char *name) {
    for (int i = 0; i < g_nconsts; i++)
        if (!strcmp(g_consts[i].name, name)) return g_consts[i].lit;
    return NULL;
}
/* `const NAME = <literal>` at module top level. Registered at parse time so any
 * function body (parsed later) can fold it. Collision with a function name is
 * caught in resolve_program (sigs aren't registered yet at parse time). */
static void parse_const(Parser *ps) {
    ps->p++;                                          /* eat contextual 'const' */
    Tok *nameT = eat(ps, TK_IDENT, "a constant name after 'const'");
    eat(ps, TK_EQ, "'=' after the constant name");
    Expr *lit = const_fold(parse_expr(ps), 1);   /* top level: also resolve backward const refs */
    if (!is_literal_expr(lit))
        die_at(lit->line, "const value must be a literal");
    char *nm = pkg_mangle(nameT->text);
    int vi;
    if (struct_find(nm) >= 0 || enum_find(nm) >= 0 || newtype_find(nm) >= 0
        || handle_find(nm) >= 0 || variant_find(nm, &vi) >= 0 || consts_find(nm))
        die_at(nameT->line, "'%s' is already defined", nameT->text);
    TBL_ENSURE(g_consts, g_nconsts, g_consts_cap);
    g_consts[g_nconsts].name = nm;
    g_consts[g_nconsts].type = lit_type(lit);
    g_consts[g_nconsts].lit  = lit;
    g_nconsts++;
}

static ProcVec parse_program(Tok *toks) {
    Parser ps = { toks, 0, 0 };
    ProcVec out = {0};
    g_parsed_package = NULL;                     /* reset per file; set if a `package` decl is seen */
    while (!at(&ps, TK_EOF)) {
        if (accept(&ps, TK_NEWLINE)) continue;
        if (at(&ps, TK_IDENT) && !strcmp(cur(&ps)->text, "package")) { parse_package_decl(&ps); continue; }
        if (at(&ps, TK_IDENT) && !strcmp(cur(&ps)->text, "import"))  { parse_import_decl(&ps);  continue; }
        if (at(&ps, TK_IDENT) && !strcmp(cur(&ps)->text, "extern") && peek(&ps, 1)->kind != TK_LPAREN) {
            Proc *pr = parse_extern_fn(&ps);
            if (out.n == out.cap) { out.cap = out.cap ? out.cap * 2 : 8; out.v = (Proc **)xrealloc(out.v, (size_t)out.cap * sizeof(Proc *)); }
            out.v[out.n++] = pr;
            continue;
        }
        if (at(&ps, TK_IDENT) && !strcmp(cur(&ps)->text, "const")) { parse_const(&ps); continue; }
        if (at(&ps, TK_STRUCT)) { parse_struct(&ps); continue; }
        if (at(&ps, TK_ENUM))   { parse_enum(&ps); continue; }
        if (at(&ps, TK_HANDLE)) { parse_handle(&ps); continue; }
        if (at(&ps, TK_TYPE))   { parse_typedecl(&ps); continue; }
        Proc *pr = parse_fn(&ps);
        if (out.n == out.cap) { out.cap = out.cap ? out.cap * 2 : 8; out.v = (Proc **)xrealloc(out.v, (size_t)out.cap * sizeof(Proc *)); }
        out.v[out.n++] = pr;
    }
    return out;
}

/* ------------------------------------------------------- function table */

typedef struct {
    const char *name;
    Type        ret;
    Type        params[16];
    int         mut[16];   /* per-param: is it a mut (by-pointer) param? */
    int         sink[16];  /* per-param: is it a `sink` (owned, mutable) param? (prototype) */
    int         nparams;
    int         builtin;
    int         is_extern;   /* FFI: call the C symbol `name` directly (no arena arg); str ret arena-copied */
} Sig;

static Sig  *g_sigs;       /* dynamic (was fixed 512; outgrown once at 256) */
static int  g_nsigs = 0, g_sigs_cap = 0;

/* FFI: link libraries named by `extern "Lib" fn` — appended as -lLib to the cc
 * line. Deduped; -lm is always passed separately (covers bare `extern fn sqrt`). */
static const char **g_links;
static int  g_nlinks = 0, g_links_cap = 0;
static void add_link(const char *lib) {
    if (!lib || !*lib) return;
    for (int i = 0; i < g_nlinks; i++) if (!strcmp(g_links[i], lib)) return;
    TBL_ENSURE(g_links, g_nlinks, g_links_cap); g_links[g_nlinks++] = lib;
}

/* FFI: companion C shims auto-discovered next to a package -- `<dir>/<pkg>_shim.c`
 * -- compiled+linked alongside the generated C, so `import "core:regex"` is
 * turnkey (no manual --shim). A package that needs an external library still
 * declares `extern "Lib" fn` (which auto-adds -lLib via add_link). */
static int file_exists(const char *p) { FILE *f = fopen(p, "r"); if (f) { fclose(f); return 1; } return 0; }
static const char **g_shims;
static int  g_nshims = 0, g_shims_cap = 0;
static void add_shim(const char *path) {
    if (!path || !*path) return;
    for (int i = 0; i < g_nshims; i++) if (!strcmp(g_shims[i], path)) return;
    TBL_ENSURE(g_shims, g_nshims, g_shims_cap); g_shims[g_nshims++] = path;
}

/* FFI: a package's external dependencies. A co-located `<dir>/deps` lists
 * pkg-config package names (one per line; blank lines and `#` comments are
 * skipped). Each resolves -- per platform, via pkg-config -- to the cflags +
 * libs the shim needs (e.g. core:http over libcurl), spliced onto the cc line
 * so a shim that #includes a system header builds turnkey. A dependency that
 * pkg-config can't resolve here surfaces as a cc error (the corelib test
 * harness probes the same `deps` and SKIPS instead). */
static char *pkg_config_flags(const char *name);   /* defined with the cc-line code below */
static char *g_pkgdeps = NULL;                      /* accumulated --cflags --libs */
static void add_pkg_deps(const char *dir) {
    char *path = sfmt("%s/deps", dir);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        char *e = s + strlen(s);
        while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
        if (!*s || *s == '#') continue;
        char *fl = pkg_config_flags(s);
        if (fl && *fl) g_pkgdeps = g_pkgdeps ? sfmt("%s %s", g_pkgdeps, fl) : sfmt("%s", fl);
        else fprintf(stderr, "tychoc: pkg-config could not resolve dependency '%s' (from %s)\n", s, path);
    }
    fclose(f);
}

static Sig *sig_find(const char *name) {
    for (int i = 0; i < g_nsigs; i++)
        if (!strcmp(g_sigs[i].name, name)) return &g_sigs[i];
    return NULL;
}

/* Generics: generic function templates (a `$T` in the signature) are kept out of
 * the Sig table; each call infers concrete type arguments, interns one monomorphic
 * instance (a real Sig + a recorded GInst), and rewrites the call to the instance.
 * The instance body is resolved + emitted from the SHARED template body during
 * codegen, sequentially per instance (see gen_program). */
static Proc **g_generics; static int g_ngenerics = 0, g_generics_cap = 0;
static Proc *generic_find(const char *name) {
    for (int i = 0; i < g_ngenerics; i++)
        if (!strcmp(g_generics[i]->name, name)) return g_generics[i];
    return NULL;
}
/* UFCS x generics: if `name` is a generic free fn whose first parameter PATTERN
 * accepts a by-value receiver of type `recv`, return its template name; else NULL.
 * Lets `x.first()` dispatch to `first(xs: [$T])`, instantiated like any call. */
static const char *ufcs_generic(const char *name, const char *pkg, Type recv) {
    Proc *gt = generic_find(name);
    if (!gt && pkg && pkg[0]) gt = generic_find(sfmt("%s%s", pkg, name));
    if (!gt) { const char *pp = type_pkg_prefix(recv); if (pp) gt = generic_find(sfmt("%s%s", pp, name)); }
    if (!gt || gt->nparams < 1 || gt->params[0].is_inout) return NULL;
    Type b[256];
    for (int i = 0; i < g_ntyparams; i++) b[i] = T_VOID;
    return match_type(gt->params[0].type, recv, b) ? gt->name : NULL;
}
/* Stage-2 generics: each instance carries its OWN cloned body — a deep copy of
 * the template body with `$T` substituted at clone time — so instances resolve
 * independently with no shared/sticky resolved state (the source of the prior
 * multi-instantiation, typed-local, and nested-call bugs). */
typedef struct { Proc *tmpl; char *name; Type params[16]; int nparams; Type ret; Type *binds; Stmt **body; int nbody; } GInst;
static GInst *g_ginsts; static int g_nginsts = 0, g_nginsts_cap = 0;
static Stmt **clone_block(Stmt **body, int n, Type *binds);   /* per-instance body clone; defined near ginst_to_proc */
static Proc **g_inst_procs; static int g_ninst_procs = 0, g_inst_procs_cap = 0;   /* resolved generic-instance Procs, shared by the prototype + body emit loops (Stage-2 #3) */
static void instantiate_generic(Proc *gt, Expr *e);   /* defined after resolve_expr */
static char *type_mangle_ident(Type t);               /* C-identifier-safe spelling of a type */

static void register_builtins(void) {
    /* designated initializers: robust to field order (mut[] sits between
     * params and nparams). All builtins are by-value (no mut). */
    TBL_RESERVE(g_sigs, 64, g_sigs_cap);   /* must exceed the builtin count below (run of appends w/o per-line ENSURE) */
    g_sigs[g_nsigs++] = (Sig){ .name="print",  .ret=T_VOID,         .params={ T_STRING },                .nparams=1, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="println",.ret=T_VOID,         .params={ T_STRING },                .nparams=1, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="eprint", .ret=T_VOID,         .params={ T_STRING },                .nparams=1, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="input",  .ret=T_STRING,       .params={ 0 },                       .nparams=0, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="read_all",.ret=T_STRING,      .params={ 0 },                       .nparams=0, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="clock",  .ret=T_INT,          .params={ 0 },                       .nparams=0, .builtin=1 };   /* monotonic nanoseconds */
    g_sigs[g_nsigs++] = (Sig){ .name="now",    .ret=T_INT,          .params={ 0 },                       .nparams=0, .builtin=1 };   /* wall-clock UNIX seconds */
    g_sigs[g_nsigs++] = (Sig){ .name="ncpu",   .ret=T_INT,          .params={ 0 },                       .nparams=0, .builtin=1 };   /* worker count = parallel-for fan-out width */
    g_sigs[g_nsigs++] = (Sig){ .name="chr",    .ret=T_STRING,       .params={ T_INT },                   .nparams=1, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="die",    .ret=T_VOID,         .params={ T_STRING },                .nparams=1, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="str",    .ret=T_STRING,       .params={ T_INT },                   .nparams=1, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="substr", .ret=T_STRING,       .params={ T_STRING, T_INT, T_INT },  .nparams=3, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="find",   .ret=T_INT,          .params={ T_STRING, T_STRING },      .nparams=2, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="split",  .ret=T_ARRAY_STRING, .params={ T_STRING, T_STRING },      .nparams=2, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="read_file",.ret=T_STRING,     .params={ T_STRING },                .nparams=1, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="write_file",.ret=T_BOOL,      .params={ T_STRING, T_STRING },      .nparams=2, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="list_dir",.ret=T_ARRAY_STRING, .params={ T_STRING },               .nparams=1, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="args",   .ret=T_ARRAY_STRING, .params={ 0 },                       .nparams=0, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="getenv", .ret=T_STRING,       .params={ T_STRING },                .nparams=1, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="is_null",.ret=T_BOOL,         .params={ T_PTR },                   .nparams=1, .builtin=1 };   /* FFI: opaque-handle NULL test */
    g_sigs[g_nsigs++] = (Sig){ .name="to_ptr", .ret=T_PTR,          .params={ T_INT },                   .nparams=1, .builtin=1 };   /* FFI: int -> opaque ptr (sentinel pointers like SQLITE_TRANSIENT = (void*)-1) */
    g_sigs[g_nsigs++] = (Sig){ .name="to_i32", .ret=T_INT,          .params={ T_INT },                   .nparams=1, .builtin=1 };   /* FFI: sign-extend a 32-bit C int return (a negative int read as 64-bit would otherwise be a huge positive) */
    /* float math (libm) -- the irreducible numeric stdlib (min/max are trivial in-language) */
    g_sigs[g_nsigs++] = (Sig){ .name="sqrt",   .ret=T_FLOAT,        .params={ T_FLOAT },                 .nparams=1, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="pow",    .ret=T_FLOAT,        .params={ T_FLOAT, T_FLOAT },        .nparams=2, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="floor",  .ret=T_FLOAT,        .params={ T_FLOAT },                 .nparams=1, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="fabs",   .ret=T_FLOAT,        .params={ T_FLOAT },                 .nparams=1, .builtin=1 };
}

/* ---------------------------------------------------- variable scoping */

/* can_mutate: may the variable's aggregate be mutated in place (push /
 * index-set)? Locals yes; parameters are immutable borrows (no). */
typedef struct { char *name; Type type; int can_mutate; Expr *lit; } Var;   /* lit != NULL: an immutable named literal (const) -- folded at each use */
static Var *g_vars;
static int g_nvars = 0, g_vars_cap = 0;
/* >=0: the next resolve_block dup-checks declarations from this g_vars index
 * (a function's top block uses its param base, so a local `:=` colliding with a
 * parameter is caught); a nested block uses its own start. Reset after one use. */
static int g_dup_base = -1;

static int  vars_mark(void) { return g_nvars; }
static void vars_restore(int m) { g_nvars = m; }
static void vars_push(const char *name, Type t, int can_mutate) {
    TBL_ENSURE(g_vars, g_nvars, g_vars_cap);
    g_vars[g_nvars].name = (char *)name;
    g_vars[g_nvars].type = t;
    g_vars[g_nvars].can_mutate = can_mutate;
    g_vars[g_nvars].lit = NULL;
    g_nvars++;
}
/* a local const: immutable, folded at use (lit carries the literal Expr) */
static void vars_push_const(const char *name, Type t, Expr *lit) {
    TBL_ENSURE(g_vars, g_nvars, g_vars_cap);
    g_vars[g_nvars].name = (char *)name;
    g_vars[g_nvars].type = t;
    g_vars[g_nvars].can_mutate = 0;
    g_vars[g_nvars].lit = lit;
    g_nvars++;
}
static Var *vars_lookup(const char *name) {   /* innermost binding, or NULL */
    for (int i = g_nvars - 1; i >= 0; i--)
        if (!strcmp(g_vars[i].name, name)) return &g_vars[i];
    return NULL;
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

static const char *suggest_var(const char *name) {
    const char *best = NULL; int bestd = 99;
    for (int i = 0; i < g_nvars; i++) dym(name, g_vars[i].name, &best, &bestd);
    return dym_pick(name, best, bestd);
}
static const char *suggest_fn(const char *name) {
    const char *best = NULL; int bestd = 99;
    for (int i = 0; i < g_nsigs; i++) dym(name, g_sigs[i].name, &best, &bestd);
    return dym_pick(name, best, bestd);
}


/* --------------------------------------------------------- type resolve */

static int is_cmp(TokKind op) {
    return op == TK_EQEQ || op == TK_NEQ || op == TK_LT ||
           op == TK_GT   || op == TK_LE  || op == TK_GE;
}

static Type resolve_exp(Expr *e, Type want);   /* defined below; fixes a None's type */

/* Are we resolving a mutable PLACE spine (where `m[k]` is a legal map-value
 * projection target)? A map index is a place only — never an rvalue read — so
 * resolve_expr rejects it unless this is set. It is captured-and-cleared at the
 * top of every resolve_expr so children are rvalues by default; only the spine
 * cases (E_INDEX base, E_FIELD/E_TUPIDX lhs) re-enable it for their spine child.
 * Statement handlers that resolve a place target (S_INDEXSET/S_FIELDSET, push's
 * first arg) set it to 1 around that one resolve. #2 (docs/map-mutation.md). */
static int g_place = 0;
static Type g_fn_ret = T_VOID;   /* return type of the proc currently being resolved (for or_return) */

/* A place we can mutate in place (take `&` of in C): a variable, a field of
 * such a place, or an element of a composite array (a projection). For an
 * ARRC element, gen_lvalue yields a pointer into the backing buffer
 * (tycho_arr_C<id>_ptr), so `arr[i].f = v` and `push(arr[i].xs, v)` mutate the
 * element in place without exposing a pointer to Tycho. A scalar-array or
 * string index is not a mutable interior, so it is never an inner lvalue. */
static int is_lvalue(Expr *e) {
    if (e->kind == E_IDENT) return 1;
    if (e->kind == E_FIELD) return is_lvalue(e->lhs);
    if (e->kind == E_TUPIDX) return is_lvalue(e->lhs);   /* t.0 = v: a tuple element is a place */
    if (e->kind == E_INDEX) return (IS_ARRC(e->lhs->type) || IS_SOA(e->lhs->type) || is_map(e->lhs->type)) && is_lvalue(e->lhs);
    return 0;
}

/* A literal of type t's zero value, for desugaring an rvalue read `m[k]` into
 * map_get(m, k, <zero>) -- a PURE read that yields the value-type zero on a
 * missing key without inserting. Only scalar value types have an unambiguous
 * zero; returns NULL otherwise (composite/newtype values keep requiring an
 * explicit map_get(m, k, default)). */
static Expr *scalar_zero_expr(Type t, int line) {
    Expr *z = NULL;
    if (t == T_INT)         { z = new_expr(E_INT,   line); z->ival = 0; }
    else if (t == T_FLOAT)  { z = new_expr(E_FLOAT, line); z->fval = 0.0; }
    else if (t == T_STRING) { z = new_expr(E_STR,   line); z->sval = ""; }
    else if (t == T_BOOL)   { z = new_expr(E_BOOL,  line); z->ival = 0; }
    return z;
}

static void resolve_block(Stmt **body, int n, Type ret);   /* fwd: the lambda body resolves as a block */

/* collect the (deduped) identifier names referenced anywhere in `e` — the basis of
 * a lambda's capture analysis (a free var that is an enclosing local is captured). */
static void collect_idents(Expr *e, const char **out, int *n, int cap) {
    if (!e) return;
    if (e->kind == E_IDENT) {
        for (int i = 0; i < *n; i++) if (!strcmp(out[i], e->sval)) return;
        if (*n < cap) out[(*n)++] = e->sval;
        return;
    }
    if (e->kind == E_LAMBDA) {   /* a nested lambda: descend so the outer transitively captures.
                                  * The inner's own params aren't enclosing locals, so the capture
                                  * filter (vars_find) drops them — only real outer locals stick. */
        collect_idents(g_laminfo[e->ival].proc->body[0]->expr, out, n, cap);
        return;
    }
    if (e->kind == E_CALL && e->sval) {   /* a call's callee may itself be a captured fn-value local
                                           * (`g(x)` where g is a closure); vars_find later keeps only
                                           * real locals — global fns/builtins/constructors are dropped. */
        int dup = 0;
        for (int i = 0; i < *n; i++) if (!strcmp(out[i], e->sval)) { dup = 1; break; }
        if (!dup && *n < cap) out[(*n)++] = e->sval;
    }
    collect_idents(e->lhs, out, n, cap);
    collect_idents(e->rhs, out, n, cap);
    for (int i = 0; i < e->nargs; i++) collect_idents(e->args[i], out, n, cap);
}

/* spawn sites, registered at resolve time so gen_program can emit one args
 * struct + thread trampoline per site (the lambda-lift pattern). */
static int *g_spawn;   /* indices into g_sigs (not Sig* -- g_sigs may realloc) */
static int g_nspawn = 0, g_spawn_cap = 0;

/* B-3 pending declarations (bidirectional inference): `xs := []` / `x := None`
 * with no context declares at T_PENDING; the FIRST grounding use in its
 * block — an assignment, push/map_set, or any expected-type position (the
 * resolve_exp head) — retroactively types the variable, its decl, and its
 * initializer node. Any use that NEEDS the type first dies at that line;
 * a still-pending var dies when its block ends (resolve_block audits). */
static struct { const char *name; Stmt *decl; int done; } g_pend[32];
static int g_npend = 0;

static int pend_find(const char *name) {          /* newest-first, not-yet-done */
    for (int i = g_npend - 1; i >= 0; i--)
        if (!g_pend[i].done && !strcmp(g_pend[i].name, name)) return i;
    return -1;
}
static void pend_ground(const char *name, Type t, int line) {
    if (t == T_VOID || t == T_NONE || t == T_OK_PARTIAL || t == T_ERR_PARTIAL || t == T_PENDING)
        die_at(line, "cannot infer the type of '%s' from this use", name);
    int pi = pend_find(name);
    if (pi < 0) return;
    Stmt *d = g_pend[pi].decl;
    if (d->expr->kind == E_ARRLIT) {              /* bare [] initializer takes the grounded type */
        if (is_map(t)) { d->expr->ival = t; d->expr->op = TK_COLON; }
        else if (is_array(t) || IS_SOA(t)) d->expr->ival = t;
        else die_at(line, "'%s' was declared with [] but its first use makes it %s", name, type_name(t));
        d->expr->type = t;
    } else {                                      /* bare None initializer needs an Option */
        if (!IS_OPT(t))
            die_at(line, "'%s' was declared with None but its first use makes it %s", name, type_name(t));
        d->expr->type = t;
    }
    d->decl_type = t;
    for (int i = g_nvars - 1; i >= 0; i--)        /* retype the live binding (newest wins) */
        if (!strcmp(g_vars[i].name, name) && g_vars[i].type == T_PENDING) { g_vars[i].type = t; break; }
    g_pend[pi].done = 1;
}

/* A deep left-leaning chain (`1+1+...`) parses iteratively (shallow parser stack,
 * so the parse_unary depth cap never fires) but builds a tree as deep as the chain
 * is long, which resolve_expr then recurses through -- guard it so a pathological
 * tree fails closed here instead of overflowing the C stack (SIGSEGV). */
#define TYCHO_MAX_TREE_DEPTH 2000
static int g_resolve_depth = 0;
static Type resolve_expr_inner(Expr *e);
static Type resolve_expr(Expr *e) {
    if (++g_resolve_depth > TYCHO_MAX_TREE_DEPTH) die_at(e->line, "expression too deeply nested to type-check (max %d)", TYCHO_MAX_TREE_DEPTH);
    Type t = resolve_expr_inner(e);
    g_resolve_depth--;
    return t;
}
static Type resolve_expr_inner(Expr *e) {
    int _place = g_place; g_place = 0;   /* children are rvalues unless a spine case re-enables (see g_place) */
    switch (e->kind) {
        case E_INT:  return e->type = T_INT;
        case E_SPAWN: {   /* spawn f(args): a named user-proc call on a new thread -> Task(ret) */
            Expr *c = e->lhs;
            resolve_expr(c);
            /* v1 fail-closed surface: a DIRECT call to a named user function.
             * Resolution may have rewritten the call -- reject everything else:
             * op==TK_FN (closure/indirect), TK_ENUM/TK_TYPE (ctor/newtype wrap),
             * kind change (struct construction), lhs (call-on-expression). */
            if (c->kind != E_CALL || c->lhs || c->op)
                die_at(e->line, "spawn requires a direct call to a named function (closures/constructors cannot be spawned yet)");
            Sig *s = sig_find(c->sval);
            if (!s || s->builtin)
                die_at(e->line, "spawn requires a user-defined function ('%s' is not one)", c->sval);
            if (s->is_extern)
                die_at(e->line, "cannot spawn an extern (FFI) function");
            if (s->ret == T_VOID)
                die_at(e->line, "a spawned function must return a value (wait(t) yields it)");
            for (int i = 0; i < s->nparams; i++)
                if (s->mut[i])
                    die_at(e->line, "cannot spawn a function with mut parameters (no shared state across threads)");
            TBL_ENSURE(g_spawn, g_nspawn, g_spawn_cap);
            g_spawn[g_nspawn] = (int)(s - g_sigs);   /* store index; g_sigs may realloc later */
            e->ival = g_nspawn++;
            return e->type = task_of(s->ret);
        }
        case E_LAMBDA: {   /* a closure literal: capture analysis + lift the body to a top-level proc */
            LamInfo *li = &g_laminfo[e->ival];
            if (li->ftype != T_VOID) return e->type = li->ftype;   /* resolve once */
            Proc *pr = li->proc;
            int nlam = pr->nparams;
            if (nlam > 8) die_at(e->line, "a lambda has at most 8 parameters");
            for (int i = 0; i < nlam; i++)     /* B-2: an untyped param needed an expected fn type */
                if (pr->params[i].type == T_VOID)
                    die_at(e->line, "lambda parameter '%s' needs a type here -- no expected fn type supplies it (annotate: fn(%s: T))",
                           pr->params[i].name, pr->params[i].name);
            const char *ids[64]; int nids = 0;
            collect_idents(pr->body[0]->expr, ids, &nids, 64);
            Param caps[16]; int ncap = 0;
            for (int i = 0; i < nids; i++) {
                int isparam = 0;
                for (int j = 0; j < nlam; j++) if (!strcmp(pr->params[j].name, ids[i])) { isparam = 1; break; }
                if (isparam) continue;
                Type vt;
                if (vars_find(ids[i], &vt)) {   /* an enclosing local -> captured BY VALUE (heap: deep-copied in) */
                    if (IS_TASK(vt)) die_at(e->line, "a closure cannot capture a task handle -- wait it first");
                    if (IS_HANDLE(vt)) die_at(e->line, "a closure cannot capture a handle -- it is freed at the end of its scope");
                    if (IS_CHAN(vt)) die_at(e->line, "a closure cannot capture a channel handle -- take it as a parameter instead");
                    if (ncap >= 16) die_at(e->line, "a lambda captures at most 16 variables");
                    caps[ncap].name = (char *)ids[i]; caps[ncap].type = vt; caps[ncap].is_inout = 0; caps[ncap].is_sink = 0; caps[ncap].ffi_ct = NULL;
                    ncap++;
                }   /* else a function/enum/global: resolves inside the lifted proc, not a capture */
            }
            /* lifted proc params become [captures...][lambda params...] (body codegen stays ordinary) */
            Param *np = (Param *)xmalloc((size_t)(ncap + nlam) * sizeof(Param));
            for (int i = 0; i < ncap; i++) np[i] = caps[i];
            for (int i = 0; i < nlam; i++) np[ncap + i] = pr->params[i];
            pr->params = np; pr->nparams = ncap + nlam;
            li->ncap = ncap;
            Type ptypes[8];
            for (int i = 0; i < nlam; i++) ptypes[i] = pr->params[ncap + i].type;
            li->ftype = funcc_of(ptypes, nlam, pr->ret);
            int mark = vars_mark();   /* resolve the body with caps + params (caps shadow the enclosing originals) */
            for (int i = 0; i < pr->nparams; i++) {
                Type pt = pr->params[i].type;
                vars_push(pr->params[i].name, pt, pr->params[i].is_sink || (!is_array(pt) && !is_map(pt) && !IS_SOA(pt)));
            }
            Type saved = g_fn_ret; g_fn_ret = pr->ret;
            g_dup_base = mark;   /* lambda body shares its caps+params scope (same lifted C function) */
            resolve_block(pr->body, pr->nbody, pr->ret);
            g_fn_ret = saved;
            vars_restore(mark);
            if (g_lambda_procs.n == g_lambda_procs.cap) { g_lambda_procs.cap = g_lambda_procs.cap ? g_lambda_procs.cap * 2 : 8; g_lambda_procs.v = (Proc **)xrealloc(g_lambda_procs.v, (size_t)g_lambda_procs.cap * sizeof(Proc *)); }
            g_lambda_procs.v[g_lambda_procs.n++] = pr;
            return e->type = li->ftype;
        }
        case E_CHAR: return e->type = T_CHAR;
        case E_FLOAT:return e->type = T_FLOAT;
        case E_NONE: return e->type = T_NONE;   /* concrete Option fixed by context */
        case E_SOME: {
            Type inner = resolve_expr(e->lhs);
            if (inner == T_VOID || inner == T_NONE)
                die_at(e->line, "Some(...) needs a concrete value");
            return e->type = opt_of(inner);
        }
        case E_OK: case E_ERR: {   /* one half of a Result; context fixes the rest */
            Type inner = resolve_expr(e->lhs);
            const char *w = e->kind == E_OK ? "Ok" : "Err";
            if (inner == T_VOID || inner == T_NONE || inner == T_OK_PARTIAL || inner == T_ERR_PARTIAL)
                die_at(e->line, "%s(...) needs a concrete value", w);
            return e->type = (e->kind == E_OK ? T_OK_PARTIAL : T_ERR_PARTIAL);
        }
        case E_TUPLE: {   /* (e1, ..., en): a tuple literal */
            if (e->nargs < 2) die_at(e->line, "a tuple needs at least two elements");
            if (e->nargs > 8) die_at(e->line, "a tuple has at most 8 elements");
            Type elems[8];
            for (int i = 0; i < e->nargs; i++) {
                Type et = resolve_expr(e->args[i]);
                if (et == T_VOID || et == T_NONE || et == T_OK_PARTIAL || et == T_ERR_PARTIAL)
                    die_at(e->line, "tuple element %d needs a concrete value", i + 1);
                elems[i] = et;
            }
            return e->type = tup_of(elems, e->nargs);
        }
        case E_TUPIDX: {   /* t.0 / t.1 */
            g_place = _place;                  /* t.i is a place iff t is (spine) */
            Type bt = resolve_expr(e->lhs);
            if (!IS_TUP(bt))
                die_at(e->line, "tuple index .%ld on a non-tuple value (%s)", e->ival, type_name(bt));
            if (e->ival < 0 || e->ival >= tup_n(bt))
                die_at(e->line, "tuple index %ld out of range (the tuple has %d elements)", e->ival, tup_n(bt));
            return e->type = tup_elem(bt, (int)e->ival);
        }
        case E_ORRETURN: {   /* unwrap Ok(v)/Some(v) to v, or short-circuit the enclosing fn with Err(e)/None */
            Type rt = resolve_expr(e->lhs);
            if (IS_OPT(rt)) {   /* Option: unwrap Some, else propagate None from an Option-returning fn */
                if (!IS_OPT(g_fn_ret))
                    die_at(e->line, "or_return on an Option requires the enclosing function to return "
                           "an Option, but it returns %s", type_name(g_fn_ret));
                return e->type = opt_inner(rt);
            }
            if (!IS_RES(rt))
                die_at(e->line, "or_return applies to a Result or Option value, got %s", type_name(rt));
            if (!IS_RES(g_fn_ret))
                die_at(e->line, "or_return requires the enclosing function to return a Result, "
                       "but it returns %s", type_name(g_fn_ret));
            if (res_err(rt) != res_err(g_fn_ret))
                die_at(e->line, "or_return propagates a %s error, but the function's error type is %s",
                       type_name(res_err(rt)), type_name(res_err(g_fn_ret)));
            return e->type = res_ok(rt);   /* the value yielded when Ok */
        }
        case E_BOOL: return e->type = T_BOOL;
        case E_NULL: return e->type = T_PTR;
        case E_STR:  return e->type = T_STRING;
        case E_IDENT: {
            Var *lv = vars_lookup(e->sval);       /* local var / const (innermost) */
            if (lv && lv->lit) {                  /* a local const: fold this use into its literal */
                Expr *k = lv->lit;
                e->kind = k->kind; e->ival = k->ival; e->fval = k->fval; e->sval = k->sval;
                return e->type = lit_type(k);
            }
            if (lv) {
                if (lv->type == T_PENDING)        /* B-3: this use NEEDS the type; grounding hasn't happened */
                    die_at(e->line, "'%s' is used before its type can be inferred -- assign/push/pass it first, or annotate the declaration", e->sval);
                return e->type = lv->type;
            }
            Expr *clit = consts_find(e->sval);    /* precedence: local var -> const -> variant -> fn */
            if (clit) {                           /* a top-level const: fold into its literal */
                e->kind = clit->kind; e->ival = clit->ival; e->fval = clit->fval; e->sval = clit->sval;
                return e->type = lit_type(clit);
            }
            int evi, eid = variant_find(e->sval, &evi);   /* a payload-less enum variant? */
            if (eid < 0 && e->pkg && e->pkg[0])           /* try this package's prefixed variant */
                eid = variant_find(sfmt("%s%s", e->pkg, e->sval), &evi);
            if (eid >= 0) {
                if (g_enums[eid].variants[evi].npayload != 0)
                    die_at(e->line, "%s carries a payload — write %s(...)", e->sval, e->sval);
                if (g_enums[eid].generic)   /* a nullary variant of a generic enum fixes no $T -- need the explicit form */
                    die_at(e->line, "%s is a variant of generic enum %s; supply the type explicitly, e.g. %s$(int)",
                           e->sval, g_enums[eid].name, e->sval);
                e->kind = E_CALL; e->op = TK_ENUM; e->ival = evi; e->nargs = 0;   /* 0-arg constructor */
                return e->type = ENUM_TYPE(eid);
            }
            Sig *fs = sig_find(e->sval);   /* a bare top-level function name used as a value */
            if (!fs && e->pkg && e->pkg[0]) {   /* a same-package function name (mangled <pkg>name), used as a value */
                char *q = sfmt("%s%s", e->pkg, e->sval);
                fs = sig_find(q);
                if (fs) e->sval = q;            /* codegen emits the prefixed <pkg>name__clo */
            }
            if (fs && !fs->builtin) {
                if (fs->nparams > 8) die_at(e->line, "a function value supports at most 8 parameters");
                for (int i = 0; i < fs->nparams; i++)
                    if (fs->mut[i]) die_at(e->line, "'%s' has a mut parameter, so it can't be a function value", e->sval);
                e->op = TK_FN;   /* mark: this E_IDENT is a function reference (codegen emits the fat value) */
                note_fnval(e->sval);   /* emit a <name>__clo thunk for it */
                return e->type = funcc_of(fs->params, fs->nparams, fs->ret);
            }
            const char *sg = suggest_var(e->sval);
            if (!sg) sg = suggest_fn(e->sval);
            if (sg) die_at(e->line, "unknown variable '%s'; did you mean '%s'?", e->sval, sg);
            die_at(e->line, "unknown variable '%s'", e->sval);
        }
        case E_ARRLIT: {
            if (e->op == TK_COLON) {           /* map literal ["k": v, ...] */
                if (e->nargs == 0)             /* empty []string: V — type carried in ival */
                    return e->type = (Type)e->ival;
                /* args interleave k0,v0,k1,v1,...; keys string, values all int
                 * or all float (the value type picks [string: int]/[string: float]). */
                Type vt = resolve_expr(e->args[1]);
                Type kt = resolve_expr(e->args[0]);
                /* map_of is the single key/value validator (mirrors the declared
                 * [K: V] type path): it routes a composite key (struct/tuple/array/
                 * fieldless-enum/newtype) to mapc_of and returns T_VOID only for a
                 * genuinely invalid key (float/bool/non-hashable). So a composite-keyed
                 * literal `[K(1): 10, Red: 1]` is accepted, matching declared maps. */
                if (map_of(kt, vt) == T_VOID)
                    die_at(e->line, "map keys must be string, int (directly or through a newtype), a fieldless enum, or a hashable struct/tuple/array; int-keyed maps support only int/float values");
                for (int i = 0; i < e->nargs; i += 2) {
                    if (resolve_expr(e->args[i]) != kt)
                        die_at(e->line, "map keys must all have the same type");
                    if (resolve_expr(e->args[i + 1]) != vt)
                        die_at(e->line, "map values must all have the same type");
                }
                return e->type = map_of(kt, vt);
            }
            if (e->nargs == 0) {               /* empty literal: type from []T, or from context (bare []) */
                if ((Type)e->ival == T_VOID)
                    die_at(e->line, "cannot type a bare [] here -- no expected type (write []T, or use it where the element type is known)");
                /* generics: a `[]$T` element type was already substituted at clone time */
                return e->type = (Type)e->ival;
            }
            Type elem = resolve_expr(e->args[0]);
            if (elem == T_VOID || elem == T_BOOL)
                die_at(e->line, "array elements must be int, float, string, a struct, an array, or an Option");
            if (elem == T_NONE)   /* the first element fixes the type, so it can't be a bare None */
                die_at(e->line, "cannot infer the array's element type from None — put a Some(...) first");
            for (int i = 1; i < e->nargs; i++)
                if (resolve_exp(e->args[i], elem) != elem)   /* coerces a None element */
                    die_at(e->line, "array elements must all have the same type");
            return e->type = arr_of(elem);
        }
        case E_INDEX: {
            g_place = _place;                  /* the base is on the place spine */
            Type bt = resolve_expr(e->lhs);
            g_place = 0;                        /* the subscript/key is always an rvalue */
            Type kt = resolve_expr(e->rhs);
            if (is_map(bt)) {                  /* m[k] -> the value type (#2) */
                Type wantk = map_key(bt);
                if (kt != wantk)
                    die_at(e->line, "map key must be %s, got %s", type_name(wantk), type_name(kt));
                Type vt = map_val(bt);
                if (!_place) {
                    /* rvalue read -> a PURE map_get (yields the value's zero on a missing
                     * key, never inserts; place uses set g_place and skip this). A SCALAR
                     * value desugars to map_get(m, k, <literal zero>) here; a COMPOSITE
                     * value is left as E_INDEX and lowered in gen_expr to map_get with
                     * (V){0} (no literal Expr for an empty array/struct). */
                    Expr *zero = scalar_zero_expr(vt, e->line);
                    if (zero) {
                        Expr *base = e->lhs, *key = e->rhs;
                        e->kind = E_CALL; e->sval = "map_get";
                        e->args = (Expr **)xmalloc(3 * sizeof(Expr *));
                        e->args[0] = base; e->args[1] = key; e->args[2] = zero;
                        e->nargs = 3; e->lhs = NULL; e->rhs = NULL;
                        return resolve_expr(e);
                    }
                }
                e->type = vt;
                return e->type;
            }
            if (kt != T_INT)
                die_at(e->line, "index must be int");
            if (is_array(bt)) return e->type = arr_elem(bt);   /* array element */
            if (IS_SOA(bt)) return e->type = soa_struct(bt);   /* soa element (only valid under .field) */
            if (bt == T_STRING) return e->type = T_INT;        /* string byte */
            die_at(e->line, "can only index an array, a string, or a map (as a place)");
        }
        case E_SLICE: {   /* xs[a:b] — a sub-range of the same array/soa type; s[a:b] -> a substring */
            Type bt = resolve_expr(e->lhs);
            if (e->rhs && resolve_expr(e->rhs) != T_INT)
                die_at(e->line, "slice start must be int");
            if (e->nargs && resolve_expr(e->args[0]) != T_INT)
                die_at(e->line, "slice end must be int");
            if (bt == T_STRING) return e->type = T_STRING;   /* a string slice is a fresh substring */
            if (!is_array(bt) && !IS_SOA(bt))
                die_at(e->line, "can only slice an array, soa, or string");
            return e->type = bt;
        }
        case E_FIELD: {
            /* `pkg.Variant` (no parens, lhs an imported package) is a payload-less
             * enum variant value — reinterpret as a 0-arg constructor, not a field. */
            if (e->lhs->kind == E_IDENT && is_imported_pkg(e->lhs->sval)) {
                check_pkg_private(e->lhs->sval, e->sval, e->line);
                char *q = sfmt("%s%s", pkg_prefix_for(e->lhs->sval), e->sval);
                int evi, eid = variant_find(q, &evi);
                if (eid < 0)
                    die_at(e->line, "package '%s' has no variant '%s'", e->lhs->sval, e->sval);
                if (g_enums[eid].variants[evi].npayload != 0)
                    die_at(e->line, "%s.%s carries a payload — write %s.%s(...)",
                           e->lhs->sval, e->sval, e->lhs->sval, e->sval);
                if (g_enums[eid].generic)   /* nullary variant of a generic enum: no $T to fix */
                    die_at(e->line, "%s.%s is a variant of a generic enum; supply the type explicitly, e.g. %s.%s$(int)",
                           e->lhs->sval, e->sval, e->lhs->sval, e->sval);
                e->kind = E_CALL; e->sval = q; e->op = TK_ENUM; e->ival = evi; e->nargs = 0;
                return e->type = ENUM_TYPE(eid);
            }
            g_place = _place;                  /* s.field is a place iff s is (spine) */
            Type bt = resolve_expr(e->lhs);
            if (!IS_STRUCT(bt))
                die_at(e->line, "'.%s' on a non-struct value", e->sval);
            StructDef *sd = &g_structs[STRUCT_ID(bt)];
            for (int i = 0; i < sd->nfields; i++)
                if (!strcmp(sd->fields[i].name, e->sval))
                    return e->type = sd->fields[i].type;
            die_at(e->line, "struct %s has no field '%s'", sd->name, e->sval);
        }
        case E_ADDR:   /* &place; only valid as a mut argument (checked at
                        * the call site). Its type is the underlying place's. */
            return e->type = resolve_expr(e->lhs);
        case E_STRUCTLIT:   /* produced by resolving E_CALL; already typed */
            return e->type;
        case E_CALL: {
            /* t.wait() / ch.send(v) / ch.recv() / ch.close() sugar on task- and
             * channel-typed locals: rewrite to the free-call form up front.
             * These live outside the Sig table, so the UFCS machinery below
             * could never resolve them. */
            { Type _tv;
              int _cm = e->sval && (!strcmp(e->sval, "wait") || !strcmp(e->sval, "send")
                     || !strcmp(e->sval, "recv") || !strcmp(e->sval, "close"));   /* sval is NULL for a call-on-expression */
              if (e->qual && !e->lhs && _cm
                  && vars_find(e->qual, &_tv) && (IS_TASK(_tv) || IS_CHAN(_tv))) {
                  Expr *recv = new_expr(E_IDENT, e->line); recv->sval = (char *)e->qual; recv->pkg = e->pkg;
                  Expr **na = (Expr **)xmalloc((size_t)(e->nargs + 1) * sizeof(Expr *));
                  na[0] = recv;
                  for (int i = 0; i < e->nargs; i++) na[i + 1] = e->args[i];
                  e->args = na; e->nargs += 1; e->qual = NULL;
              } }
            /* `h.field(args)` where `h` is a LOCAL VARIABLE (not a package): a call
             * through a fn-typed struct field. Rewrite to a call-on-expression of
             * the field access (the parser couldn't tell h from a package name). */
            { Type _qvt;
              if (e->qual && !e->lhs && vars_find(e->qual, &_qvt)) {
                  /* x.foo(args), x a local var: a fn-typed-FIELD call takes precedence;
                   * otherwise UFCS — a free fn `foo` (or `<pkg>foo`) whose first
                   * parameter has x's type by value -> foo(x, args). Static dispatch
                   * on x's type; no classes, no inheritance. */
                  int fnfield = 0;
                  if (IS_STRUCT(_qvt)) {
                      StructDef *sd = &g_structs[STRUCT_ID(_qvt)];
                      for (int i = 0; i < sd->nfields; i++)
                          if (!strcmp(sd->fields[i].name, e->sval) && IS_FUNC(sd->fields[i].type)) { fnfield = 1; break; }
                  }
                  Sig *ms = NULL;
                  if (!fnfield) {
                      Sig *c1 = sig_find(e->sval);
                      if (c1 && !c1->builtin && c1->nparams >= 1 && c1->params[0] == _qvt && !c1->mut[0]) ms = c1;
                      if (!ms && e->pkg && e->pkg[0]) {
                          Sig *c2 = sig_find(sfmt("%s%s", e->pkg, e->sval));
                          if (c2 && !c2->builtin && c2->nparams >= 1 && c2->params[0] == _qvt && !c2->mut[0]) ms = c2;
                      }
                      if (!ms) {   /* method defined in the receiver type's package: geom__Circle -> geom__area */
                          const char *pp = type_pkg_prefix(_qvt);
                          if (pp) {
                              Sig *c3 = sig_find(sfmt("%s%s", pp, e->sval));
                              if (c3 && !c3->builtin && c3->nparams >= 1 && c3->params[0] == _qvt && !c3->mut[0]) ms = c3;
                          }
                      }
                  }
                  const char *gennm = (!ms && !fnfield) ? ufcs_generic(e->sval, e->pkg, _qvt) : NULL;
                  if (ms || gennm) {   /* UFCS: prepend the receiver, drop the qualifier -> foo(x, args) */
                      Expr *recv = new_expr(E_IDENT, e->line); recv->sval = (char *)e->qual; recv->pkg = e->pkg;
                      Expr **na = (Expr **)xmalloc((size_t)(e->nargs + 1) * sizeof(Expr *));
                      na[0] = recv;
                      for (int i = 0; i < e->nargs; i++) na[i + 1] = e->args[i];
                      e->args = na; e->nargs += 1;
                      e->qual = NULL;
                      e->sval = ms ? (char *)ms->name : (char *)gennm;   /* method name; a generic template name is instantiated by the dispatch below */
                  } else {            /* fn-typed-field call: a call-on-expression of x.foo */
                      Expr *base = new_expr(E_IDENT, e->line); base->sval = (char *)e->qual; base->pkg = e->pkg;
                      Expr *fld = new_expr(E_FIELD, e->line); fld->lhs = base; fld->sval = e->sval;
                      e->lhs = fld; e->qual = NULL; e->sval = NULL;
                  }
              } }
            /* Stage 2 UFCS: callee is `base.name` (E_FIELD) where name is not a
             * fn-typed field of base's type but IS a method -> name(base, args).
             * Enables method chaining on any receiver expression: a.f().g(). */
            if (e->lhs && e->lhs->kind == E_FIELD) {
                Expr *fld = e->lhs;
                Type bt = resolve_expr(fld->lhs);
                int fnfield = 0;
                if (IS_STRUCT(bt)) {
                    StructDef *sd = &g_structs[STRUCT_ID(bt)];
                    for (int i = 0; i < sd->nfields; i++)
                        if (!strcmp(sd->fields[i].name, fld->sval) && IS_FUNC(sd->fields[i].type)) { fnfield = 1; break; }
                }
                Sig *ms = NULL;
                if (!fnfield) {
                    Sig *c1 = sig_find(fld->sval);
                    if (c1 && !c1->builtin && c1->nparams >= 1 && c1->params[0] == bt && !c1->mut[0]) ms = c1;
                    if (!ms && e->pkg && e->pkg[0]) {
                        Sig *c2 = sig_find(sfmt("%s%s", e->pkg, fld->sval));
                        if (c2 && !c2->builtin && c2->nparams >= 1 && c2->params[0] == bt && !c2->mut[0]) ms = c2;
                    }
                    if (!ms) {   /* method defined in the receiver type's package */
                        const char *pp = type_pkg_prefix(bt);
                        if (pp) {
                            Sig *c3 = sig_find(sfmt("%s%s", pp, fld->sval));
                            if (c3 && !c3->builtin && c3->nparams >= 1 && c3->params[0] == bt && !c3->mut[0]) ms = c3;
                        }
                    }
                }
                const char *gennm2 = (!ms && !fnfield) ? ufcs_generic(fld->sval, e->pkg, bt) : NULL;
                if (ms || gennm2) {   /* prepend base as the receiver; resolve as a normal named call */
                    Expr **na = (Expr **)xmalloc((size_t)(e->nargs + 1) * sizeof(Expr *));
                    na[0] = fld->lhs;
                    for (int i = 0; i < e->nargs; i++) na[i + 1] = e->args[i];
                    e->args = na; e->nargs += 1;
                    e->sval = ms ? (char *)ms->name : (char *)gennm2;   /* generic template name dispatches below */
                    e->lhs = NULL;
                }
            }
            if (e->lhs) {   /* call-on-expression: an indirect call on a fn VALUE (array elem, struct field, call result) */
                Type ct = resolve_exp(e->lhs, T_VOID);
                if (!IS_FUNC(ct)) die_at(e->line, "calling a value that isn't a function (%s)", type_name(ct));
                if (e->nargs != func_n(ct))
                    die_at(e->line, "this function value expects %d argument(s), got %d", func_n(ct), e->nargs);
                for (int i = 0; i < e->nargs; i++)
                    if (resolve_exp(e->args[i], func_param(ct, i)) != func_param(ct, i))
                        die_at(e->line, "argument %d must be %s", i + 1, type_name(func_param(ct, i)));
                e->op = TK_FN;   /* indirect-call marker */
                return e->type = func_ret(ct);
            }
            Type fvt;   /* indirect call through a function-typed local variable: f(args) */
            if (!e->qual && vars_find(e->sval, &fvt) && IS_FUNC(fvt)) {
                if (e->nargs != func_n(fvt))
                    die_at(e->line, "'%s' expects %d argument(s), got %d", e->sval, func_n(fvt), e->nargs);
                for (int i = 0; i < e->nargs; i++)
                    if (resolve_exp(e->args[i], func_param(fvt, i)) != func_param(fvt, i))
                        die_at(e->line, "argument %d to '%s' must be %s", i + 1, e->sval, type_name(func_param(fvt, i)));
                e->op = TK_FN;   /* indirect-call marker; gen_call's user-proc tail emits h_<var>(arena, args) */
                return e->type = func_ret(fvt);
            }
            /* Package resolution (Stage B): rewrite e->sval to the package-mangled
             * name before any lookup. An explicit `pkg.name` (e->qual) MUST resolve
             * in that package; an implicit name in an imported package (e->pkg) tries
             * its own package first, else falls through to builtins/unprefixed. */
            if (e->qual) {
                check_pkg_private(e->qual, e->sval, e->line);
                int _vi;
                char *q = sfmt("%s%s", pkg_prefix_for(e->qual), e->sval);
                if (sig_find(q) || struct_find(q) >= 0 || newtype_find(q) >= 0 || variant_find(q, &_vi) >= 0)
                    e->sval = q;
                else if (generic_find(q))     /* a generic template lives in the generics registry, not a Sig */
                    { e->sval = q; e->qual = NULL; }   /* adopt the mangled name + drop qual so the generic dispatch below instantiates it */
                else
                    die_at(e->line, "package '%s' has no symbol '%s'", e->qual, e->sval);
            } else if (e->pkg && e->pkg[0]) {
                int _vi;
                char *q = sfmt("%s%s", e->pkg, e->sval);
                if (sig_find(q) || struct_find(q) >= 0 || newtype_find(q) >= 0 || variant_find(q, &_vi) >= 0)
                    e->sval = q;
                else if (generic_find(q))      /* a package-local generic: rewrite so the generic dispatch below instantiates it */
                    e->sval = q;
            }
            /* a call whose name is a newtype wraps its underlying value: Meters(x)
             * with x : float -> Meters. Zero-cost; codegen is the identity. */
            int ntid = newtype_find(e->sval);
            if (ntid >= 0) {
                Type under = g_newtypes[ntid].under;
                if (e->nargs != 1)
                    die_at(e->line, "%s(x) takes one %s", e->sval, type_name(under));
                Type at_ = resolve_exp(e->args[0], under);   /* expected type grounds a bare []/[:] literal */
                if (at_ != under)
                    die_at(e->line, "%s(x) needs a %s, got %s", e->sval, type_name(under), type_name(at_));
                e->op = TK_TYPE;   /* mark as a newtype wrap for codegen (identity) */
                return e->type = NT_TYPE(ntid);
            }
            /* a call whose name is a struct is positional construction */
            int sid = struct_find(e->sval);
            if (sid >= 0 && g_structs[sid].generic) {   /* generic struct: infer the type args from the field values */
                StructDef *t = &g_structs[sid];
                if (e->nargs != t->nfields)
                    die_at(e->line, "%s takes %d field value(s), got %d", t->name, t->nfields, e->nargs);
                Type binds[256];
                for (int i = 0; i < g_ntyparams; i++) binds[i] = T_VOID;
                for (int i = 0; i < e->nargs; i++) {
                    Type at_ = resolve_expr(e->args[i]);
                    if (!match_type(t->fields[i].type, at_, binds))
                        die_at(e->line, "field '%s' of %s does not fit a %s argument",
                               t->fields[i].name, t->name, type_name(at_));
                }
                for (int i = 0; i < t->ntyparams; i++)
                    if (binds[(int)(t->typarams[i] - T_TYPARAM_BASE)] == T_VOID)
                        die_at(e->line, "type parameter $%s of %s is not fixed by any field value",
                               typaram_name(t->typarams[i]), t->name);
                sid = struct_instantiate(sid, binds);
            }
            if (sid >= 0) {
                StructDef *sd = &g_structs[sid];
                if (e->nargs != sd->nfields)
                    die_at(e->line, "%s takes %d field value(s), got %d",
                           sd->name, sd->nfields, e->nargs);
                for (int i = 0; i < e->nargs; i++) {
                    Type at_ = resolve_exp(e->args[i], sd->fields[i].type);   /* fixes a None field */
                    if (at_ != sd->fields[i].type)
                        die_at(e->line, "field '%s' of %s is %s, got %s",
                               sd->fields[i].name, sd->name,
                               type_name(sd->fields[i].type), type_name(at_));
                }
                e->kind = E_STRUCTLIT;          /* reinterpret for codegen */
                return e->type = STRUCT_TYPE(sid);
            }
            /* a call whose name is an enum variant is a constructor */
            {
                int evi, eid = variant_find(e->sval, &evi);
                if (eid >= 0 && g_enums[eid].generic) {   /* generic enum: fix $T from explicit type args and/or the payload values, then instantiate */
                    EnumDef *gt = &g_enums[eid];
                    Variant *gv = &gt->variants[evi];
                    Type binds[256];
                    for (int i = 0; i < g_ntyparams; i++) binds[i] = T_VOID;
                    if (e->ntypeargs > 0) {        /* `Leaf$(int)`: explicit -- the only way to fix a nullary variant */
                        if (e->ntypeargs != gt->ntyparams)
                            die_at(e->line, "%s expects %d type argument(s), got %d", gt->name, gt->ntyparams, e->ntypeargs);
                        for (int i = 0; i < gt->ntyparams; i++)
                            binds[(int)(gt->typarams[i] - T_TYPARAM_BASE)] = e->typeargs[i];
                    }
                    if (e->nargs != gv->npayload)
                        die_at(e->line, "%s takes %d payload value(s), got %d", gv->name, gv->npayload, e->nargs);
                    for (int i = 0; i < e->nargs; i++) {   /* infer $T by matching each payload pattern against the arg type */
                        Type at_ = resolve_expr(e->args[i]);
                        if (!match_type(gv->payload[i], at_, binds))
                            die_at(e->line, "%s payload %d does not fit a %s argument", gv->name, i + 1, type_name(at_));
                    }
                    for (int i = 0; i < gt->ntyparams; i++)
                        if (binds[(int)(gt->typarams[i] - T_TYPARAM_BASE)] == T_VOID)
                            die_at(e->line, "type parameter $%s of %s is not fixed by any payload value; supply it explicitly, e.g. %s$(int)",
                                   typaram_name(gt->typarams[i]), gt->name, e->sval);
                    eid = enum_instantiate(eid, binds);   /* eid now names the concrete instance; the tail below re-resolves against it */
                }
                if (eid >= 0) {
                    Variant *var = &g_enums[eid].variants[evi];
                    if (e->nargs != var->npayload)
                        die_at(e->line, "%s takes %d payload value(s), got %d", var->name, var->npayload, e->nargs);
                    for (int i = 0; i < e->nargs; i++) {
                        Type at_ = resolve_exp(e->args[i], var->payload[i]);   /* coerces a None */
                        if (at_ != var->payload[i])
                            die_at(e->line, "%s payload %d is %s, got %s", var->name, i + 1,
                                   type_name(var->payload[i]), type_name(at_));
                    }
                    e->op = TK_ENUM; e->ival = evi;   /* mark as an enum ctor; carry the variant index */
                    return e->type = ENUM_TYPE(eid);
                }
            }
            /* wait(t): join a spawned task and yield its result, deep-copied
             * into the waiting scope's arena; the task's arena tree is freed.
             * The one consumer of a Task value (which has no type syntax). */
            if (!strcmp(e->sval, "wait")) {
                if (e->nargs != 1) die_at(e->line, "wait(t) takes one task");
                Type at_ = resolve_expr(e->args[0]);
                if (!IS_TASK(at_)) die_at(e->line, "wait(t) takes a task from spawn, got %s", type_name(at_));
                if (e->args[0]->kind != E_IDENT && e->args[0]->kind != E_SPAWN)
                    die_at(e->line, "wait takes a task variable or a spawn expression");
                return e->type = task_inner(at_);
            }
            /* channel(T, cap): legal only as the direct RHS of a declaration --
             * the creating variable's scope owns the channel and frees it at
             * scope exit (after CC-2's implicit joins). S_DECL marks that. */
            if (!strcmp(e->sval, "channel") && e->ival) {
                if (e->op != TK_COLONEQ)
                    die_at(e->line, "a channel must be created directly in a declaration: ch := channel(T, cap)");
                e->op = 0;   /* consume the marker */
                if (resolve_exp(e->args[0], T_INT) != T_INT)
                    die_at(e->line, "channel(T, cap) needs an int capacity");
                return e->type = chan_of((Type)e->ival);
            }
            if (!strcmp(e->sval, "send")) {   /* send(ch, v): deep-copy v into the channel; blocks when full */
                if (e->nargs != 2) die_at(e->line, "send(ch, v) takes a channel and a value");
                Type ct = resolve_expr(e->args[0]);
                if (!IS_CHAN(ct)) die_at(e->line, "send(ch, v) takes a channel, got %s", type_name(ct));
                Type want = chan_inner(ct);
                if (resolve_exp(e->args[1], want) != want)
                    die_at(e->line, "send on %s needs a %s value", type_name(ct), type_name(want));
                return e->type = T_VOID;
            }
            if (!strcmp(e->sval, "recv")) {   /* recv(ch) -> Option(T): blocks; None = closed and drained */
                if (e->nargs != 1) die_at(e->line, "recv(ch) takes one channel");
                Type ct = resolve_expr(e->args[0]);
                if (!IS_CHAN(ct)) die_at(e->line, "recv(ch) takes a channel, got %s", type_name(ct));
                return e->type = opt_of(chan_inner(ct));
            }
            if (!strcmp(e->sval, "close")) {  /* close(ch): receivers drain then see None; close(h): free a handle early */
                if (e->nargs != 1) die_at(e->line, "close takes one channel or handle");
                Type ct = resolve_expr(e->args[0]);
                if (IS_HANDLE(ct)) {   /* FFI R2: early close -- run the destructor now, suppress the scope-exit free */
                    if (e->args[0]->kind != E_IDENT)
                        die_at(e->line, "close(h) takes a handle variable");
                    return e->type = T_VOID;
                }
                if (!IS_CHAN(ct)) die_at(e->line, "close takes a channel or a handle, got %s", type_name(ct));
                return e->type = T_VOID;
            }
            /* str is polymorphic (int or float); to_int/to_float convert
             * between the two (no implicit mixing exists). Handled inline so
             * they bypass the fixed-signature Sig table. */
            if (!strcmp(e->sval, "str")) {
                if (e->nargs != 1) die_at(e->line, "str(x) takes one argument");
                Type b = base_of(resolve_expr(e->args[0]));   /* sees through a newtype */
                if (b != T_INT && b != T_BOOL && b != T_FLOAT && b != T_STRING &&
                    b != T_U32 && b != T_U64 && b != T_F32)   /* string: identity (for interpolation) */
                    die_at(e->line, "str(x) takes an int, a u32/u64/f32, a bool, a float, or a string");
                return e->type = T_STRING;
            }
            if (!strcmp(e->sval, "to_float")) {   /* int/u32/u64/f32 -> float, or unwrap a float newtype */
                if (e->nargs != 1) die_at(e->line, "to_float(n) takes one argument");
                Type at_ = resolve_expr(e->args[0]);
                if (at_ != T_INT && at_ != T_U32 && at_ != T_U64 && at_ != T_F32 && !(IS_NEWTYPE(at_) && nt_under(at_) == T_FLOAT))
                    die_at(e->line, "to_float(n) takes an int, a u32/u64/f32, or a float newtype");
                return e->type = T_FLOAT;
            }
            if (!strcmp(e->sval, "to_int")) {   /* float/u32/u64/f32 -> int (truncate), or unwrap an int newtype */
                if (e->nargs != 1) die_at(e->line, "to_int(x) takes one argument");
                Type at_ = resolve_expr(e->args[0]);
                if (at_ != T_FLOAT && at_ != T_U32 && at_ != T_U64 && at_ != T_F32 && !(IS_NEWTYPE(at_) && nt_under(at_) == T_INT))
                    die_at(e->line, "to_int(x) takes a float (truncates toward zero), a u32/u64/f32, or an int newtype");
                return e->type = T_INT;
            }
            if (!strcmp(e->sval, "to_u32") || !strcmp(e->sval, "to_u64") || !strcmp(e->sval, "to_f32")) {
                if (e->nargs != 1) die_at(e->line, "%s(x) takes one argument", e->sval);
                Type at_ = base_of(resolve_expr(e->args[0]));   /* any numeric scalar -> the sized type */
                if (at_ != T_INT && at_ != T_CHAR && at_ != T_FLOAT && at_ != T_U32 && at_ != T_U64 && at_ != T_F32)
                    die_at(e->line, "%s(x) takes a numeric value (int/char/float/u32/u64/f32)", e->sval);
                return e->type = !strcmp(e->sval, "to_u32") ? T_U32 : !strcmp(e->sval, "to_u64") ? T_U64 : T_F32;
            }
            if (!strcmp(e->sval, "to_str")) {   /* unwrap a string newtype, OR reinterpret bytes -> string */
                if (e->nargs != 1) die_at(e->line, "to_str(x) takes one argument");
                Type at_ = resolve_expr(e->args[0]);
                if (at_ != T_BYTES && !(IS_NEWTYPE(at_) && nt_under(at_) == T_STRING))
                    die_at(e->line, "to_str(x) takes bytes or a string newtype");
                return e->type = T_STRING;
            }
            if (!strcmp(e->sval, "to_ptr")) {   /* int -> opaque ptr: a sentinel pointer for C (e.g. SQLITE_TRANSIENT = (void*)-1). tycho never derefs it. */
                if (e->nargs != 1) die_at(e->line, "to_ptr(n) takes one argument");
                if (resolve_expr(e->args[0]) != T_INT)
                    die_at(e->line, "to_ptr(n) takes an int");
                return e->type = T_PTR;
            }
            if (!strcmp(e->sval, "to_i32")) {   /* reinterpret the low 32 bits of an int as a signed C `int`, sign-extended -- for extern fns that return a 32-bit `int` (esp. a negative one). */
                if (e->nargs != 1) die_at(e->line, "to_i32(n) takes one argument");
                if (resolve_expr(e->args[0]) != T_INT)
                    die_at(e->line, "to_i32(n) takes an int");
                return e->type = T_INT;
            }
            if (!strcmp(e->sval, "to_bytes")) {   /* string -> bytes: same byte buffer, distinct type (FFI crosses as (ptr,len)) */
                if (e->nargs != 1) die_at(e->line, "to_bytes(s) takes one argument");
                Type at_ = base_of(resolve_expr(e->args[0]));
                if (at_ != T_STRING && at_ != T_BYTES)
                    die_at(e->line, "to_bytes(x) takes a string");
                return e->type = T_BYTES;
            }
            if (!strcmp(e->sval, "to_bool")) {   /* unwrap a bool newtype -> bool */
                if (e->nargs != 1) die_at(e->line, "to_bool(x) takes one argument");
                Type at_ = resolve_expr(e->args[0]);
                if (!(IS_NEWTYPE(at_) && nt_under(at_) == T_BOOL))
                    die_at(e->line, "to_bool(x) takes a bool newtype");
                return e->type = T_BOOL;
            }
            if (!strcmp(e->sval, "to_under")) {   /* generic newtype unwrap -> its underlying type */
                if (e->nargs != 1) die_at(e->line, "to_under(x) takes one argument");
                Type at_ = resolve_expr(e->args[0]);
                if (!IS_NEWTYPE(at_))
                    die_at(e->line, "to_under(x) takes a newtype value, got %s", type_name(at_));
                return e->type = nt_under(at_);
            }
            /* array builtins (don't fit the scalar Sig table) */
            if (!strcmp(e->sval, "len")) {
                if (e->nargs != 1) die_at(e->line, "len(...) takes one argument");
                Type at_ = resolve_expr(e->args[0]);
                if (!is_array(at_) && at_ != T_STRING && at_ != T_BYTES && !is_map(at_) && !IS_SOA(at_))
                    die_at(e->line, "len(...) takes an array, a string, bytes, a map, or a soa");
                return e->type = T_INT;
            }
            /* map builtins ([string: int] or [string: float]). The value type
             * follows the map. map_set is pure (returns a new map); the
             * m = map_set(m, ...) self-rebind is grown in place by the
             * accumulator pass, like array push / string append. */
            if (!strcmp(e->sval, "map_set")) {
                if (e->nargs != 3) die_at(e->line, "map_set(m, key, value) takes three arguments");
                {   /* B-3: map_set(m, k, v) grounds a pending m to [typeof(k): typeof(v)] */
                    Type pvt;
                    if (e->args[0]->kind == E_IDENT && vars_find(e->args[0]->sval, &pvt) && pvt == T_PENDING) {
                        Type gk = resolve_expr(e->args[1]), gv = resolve_expr(e->args[2]);
                        Type gm = map_of(gk, gv);
                        if (gm == T_VOID) die_at(e->line, "map keys must be string or int");
                        pend_ground(e->args[0]->sval, gm, e->line);
                    }
                }
                Type mt = resolve_expr(e->args[0]);
                if (!is_map(mt)) die_at(e->line, "map_set's first argument must be a map");
                if (resolve_expr(e->args[1]) != map_key(mt)) die_at(e->line, "map_set key must be %s", type_name(map_key(mt)));
                if (resolve_expr(e->args[2]) != map_val(mt))
                    die_at(e->line, "map_set value must be %s", type_name(map_val(mt)));
                return e->type = mt;
            }
            if (!strcmp(e->sval, "map_get")) {
                if (e->nargs != 3) die_at(e->line, "map_get(m, key, default) takes three arguments");
                Type mt = resolve_expr(e->args[0]);
                if (!is_map(mt)) die_at(e->line, "map_get's first argument must be a map");
                if (resolve_expr(e->args[1]) != map_key(mt)) die_at(e->line, "map_get key must be %s", type_name(map_key(mt)));
                if (resolve_expr(e->args[2]) != map_val(mt))
                    die_at(e->line, "map_get default must be %s", type_name(map_val(mt)));
                return e->type = map_val(mt);
            }
            if (!strcmp(e->sval, "map_has")) {
                if (e->nargs != 2) die_at(e->line, "map_has(m, key) takes two arguments");
                Type mt = resolve_expr(e->args[0]);
                if (!is_map(mt))
                    die_at(e->line, "map_has's first argument must be a map");
                if (resolve_expr(e->args[1]) != map_key(mt)) die_at(e->line, "map_has key must be %s", type_name(map_key(mt)));
                return e->type = T_BOOL;
            }
            /* map_del is pure (returns a new map); the m = map_del(m, k)
             * self-rebind is rewritten to an in-place backward-shift delete. */
            if (!strcmp(e->sval, "map_del")) {
                if (e->nargs != 2) die_at(e->line, "map_del(m, key) takes two arguments");
                Type mt = resolve_expr(e->args[0]);
                if (!is_map(mt)) die_at(e->line, "map_del's first argument must be a map");
                if (resolve_expr(e->args[1]) != map_key(mt)) die_at(e->line, "map_del key must be %s", type_name(map_key(mt)));
                return e->type = mt;
            }
            /* keys(m) -> [string] or [int]: the map's live keys, for iteration. */
            if (!strcmp(e->sval, "keys")) {
                if (e->nargs != 1) die_at(e->line, "keys(m) takes one argument");
                Type mt = resolve_expr(e->args[0]);
                if (!is_map(mt))
                    die_at(e->line, "keys's argument must be a map");
                return e->type = arr_of(map_key(mt));   /* [K]: a newtype key stays wrapped */
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
                {   /* B-3: push(xs, v) grounds a pending xs to [typeof(v)] */
                    Type pvt;
                    if (e->args[0]->kind == E_IDENT && vars_find(e->args[0]->sval, &pvt) && pvt == T_PENDING)
                        pend_ground(e->args[0]->sval, arr_of(resolve_expr(e->args[1])), e->line);
                }
                g_place = 1;                       /* push(m[k], v): m[k] is a place here (#2) */
                Type arrt = resolve_expr(e->args[0]);
                g_place = 0;
                if (!is_array(arrt) && !IS_SOA(arrt))
                    die_at(e->line, "push's first argument must be an array or soa");
                if (!is_lvalue(e->args[0]))
                    die_at(e->line, "cannot push through this expression — the array must be a "
                                    "variable, field, or composite-array element");
                /* push through a heap mut array is allowed: the regrow
                 * targets the value's owning arena (carried as _ina_<name>),
                 * so the new buffer outlives the call and the caller sees the
                 * updated descriptor. */
                if (!vars_can_mutate(root->sval))
                    die_at(e->line, "cannot mutate parameter '%s' (it is borrowed read-only; copy it with `y := %s` first)",
                           root->sval, root->sval);
                Type want = IS_SOA(arrt) ? soa_struct(arrt) : arr_elem(arrt);
                if (resolve_exp(e->args[1], want) != want)   /* coerces a None value */
                    die_at(e->line, "push's value must be %s", type_name(want));
                return e->type = T_VOID;
            }
            if (!strcmp(e->sval, "pop")) {   /* pop(arr): remove + return the last element */
                if (e->nargs != 1) die_at(e->line, "pop(arr) takes one argument");
                Expr *root = e->args[0];
                while (root->kind == E_FIELD || root->kind == E_INDEX) root = root->lhs;
                if (root->kind != E_IDENT)
                    die_at(e->line, "pop's first argument must be an array variable or field");
                g_place = 1;
                Type arrt = resolve_expr(e->args[0]);
                g_place = 0;
                if (!is_array(arrt) && !IS_SOA(arrt))
                    die_at(e->line, "pop's first argument must be an array or soa");
                if (!is_lvalue(e->args[0]))
                    die_at(e->line, "cannot pop through this expression — the array must be a "
                                    "variable, field, or composite-array element");
                if (!vars_can_mutate(root->sval))
                    die_at(e->line, "cannot mutate parameter '%s' (it is borrowed read-only; copy it with `y := %s` first)",
                           root->sval, root->sval);
                return e->type = IS_SOA(arrt) ? soa_struct(arrt) : arr_elem(arrt);
            }
            if (!strcmp(e->sval, "reserve")) {
                if (e->nargs != 2) die_at(e->line, "reserve(arr, n) takes two arguments");
                Expr *root = e->args[0];
                while (root->kind == E_FIELD || root->kind == E_INDEX) root = root->lhs;
                if (root->kind != E_IDENT)
                    die_at(e->line, "reserve's first argument must be an array variable or field");
                g_place = 1;                       /* reserve(m[k], n): m[k] is a place here (#2) */
                Type arrt = resolve_expr(e->args[0]);
                g_place = 0;
                if (!is_array(arrt))
                    die_at(e->line, "reserve's first argument must be an array");
                /* reserve is a capacity hint: the scalar arrays have a runtime
                 * tycho_arr_{int,float,str}_reserve; composite-element arrays get a
                 * generated tycho_arr_C%d_reserve (emitted with the family). SOA and
                 * other non-array element kinds have no reserve — fail closed. */
                if (arrt != T_ARRAY_INT && arrt != T_ARRAY_FLOAT && arrt != T_ARRAY_STRING && !IS_ARRC(arrt))
                    die_at(e->line, "reserve only supports arrays of scalars, structs, tuples, or nested arrays");
                if (!is_lvalue(e->args[0]))
                    die_at(e->line, "cannot reserve through this expression");
                if (!vars_can_mutate(root->sval))
                    die_at(e->line, "cannot mutate parameter '%s' (it is borrowed read-only)", root->sval);
                if (resolve_exp(e->args[1], T_INT) != T_INT)
                    die_at(e->line, "reserve's capacity must be int");
                return e->type = T_VOID;
            }
            { Proc *gt = generic_find(e->sval);   /* generics: infer type args, intern instance, rewrite e->sval */
              if (gt && !e->qual && !e->lhs) instantiate_generic(gt, e);
              else if (e->ntypeargs > 0) die_at(e->line, "explicit type arguments given, but '%s' is not a generic function", e->sval); }
            Sig *s = sig_find(e->sval);
            if (!s) {
                const char *sg = suggest_fn(e->sval);
                if (sg) die_at(e->line, "unknown procedure '%s'; did you mean '%s'?", e->sval, sg);
                die_at(e->line, "unknown procedure '%s'", e->sval);
            }
            if (e->nargs != s->nparams)
                die_at(e->line, "'%s' takes %d argument(s), got %d",
                       e->sval, s->nparams, e->nargs);
            for (int i = 0; i < e->nargs; i++) {
                Type at_ = resolve_exp(e->args[i], s->params[i]);   /* fixes a None arg */
                /* mut parameter: the argument must be `&place` naming a
                 * mutable variable (an lvalue we can write back through). A
                 * by-value param rejects `&`. */
                if (s->mut[i]) {
                    if (e->args[i]->kind != E_ADDR)
                        die_at(e->line, "argument %d of '%s' is mut; pass it as '&variable'",
                               i + 1, e->sval);
                    Expr *tgt = e->args[i]->lhs;
                    Expr *root = tgt;
                    while (root->kind == E_FIELD || root->kind == E_INDEX) root = root->lhs;
                    if (root->kind != E_IDENT)
                        die_at(e->line, "mut argument %d of '%s' must name a variable", i + 1, e->sval);
                    if (!vars_can_mutate(root->sval))
                        die_at(e->line, "cannot pass borrowed parameter '%s' as mut (it is read-only; copy it first)", root->sval);
                } else if (e->args[i]->kind == E_ADDR) {
                    die_at(e->line, "argument %d of '%s' is not mut; remove the '&'", i + 1, e->sval);
                }
                if (at_ != s->params[i])
                    die_at(e->line, "argument %d of '%s' is %s, expected %s",
                           i + 1, e->sval, type_name(at_), type_name(s->params[i]));
            }
            /* exclusivity (Law of Exclusivity): the same variable may not be
             * passed to two mut params of one call — that would be two
             * overlapping writes, breaking the x = f(x) value-semantics model.
             * No globals/closures exist in Tycho, so this is the only alias. */
            for (int i = 0; i < e->nargs; i++) {
                if (!s->mut[i]) continue;
                Expr *ri = e->args[i]->lhs;
                while (ri->kind == E_FIELD || ri->kind == E_INDEX) ri = ri->lhs;
                for (int j = i + 1; j < e->nargs; j++) {
                    if (!s->mut[j]) continue;
                    Expr *rj = e->args[j]->lhs;
                    while (rj->kind == E_FIELD || rj->kind == E_INDEX) rj = rj->lhs;
                    if (ri->kind == E_IDENT && rj->kind == E_IDENT && !strcmp(ri->sval, rj->sval))
                        die_at(e->line, "variable '%s' passed to two mut parameters of '%s' (overlapping mutable access)", ri->sval, e->sval);
                }
            }
            /* A slice argument views its source's buffer; a mut of that same
             * variable in the same call could reallocate the buffer (e.g. push),
             * leaving the slice dangling. Forbid the overlap. */
            for (int i = 0; i < e->nargs; i++) {
                if (e->args[i]->kind != E_SLICE) continue;
                Expr *si = e->args[i]->lhs;
                while (si->kind == E_FIELD || si->kind == E_INDEX || si->kind == E_SLICE) si = si->lhs;
                if (si->kind != E_IDENT) continue;
                for (int j = 0; j < e->nargs; j++) {
                    if (!s->mut[j]) continue;
                    Expr *rj = e->args[j]->lhs;
                    while (rj->kind == E_FIELD || rj->kind == E_INDEX) rj = rj->lhs;
                    if (rj->kind == E_IDENT && !strcmp(rj->sval, si->sval))
                        die_at(e->line, "cannot pass a slice of '%s' and a mut of '%s' in one call "
                               "(the mut may reallocate the buffer the slice views)", si->sval, si->sval);
                }
            }
            return e->type = s->ret;
        }
        case E_BINOP: {
            if (e->op == TK_NOT) {              /* unary: operand in lhs, rhs NULL */
                if (resolve_expr(e->lhs) != T_BOOL)
                    die_at(e->line, "`not` needs a bool operand");
                return e->type = T_BOOL;
            }
            if (e->op == TK_MINUS && e->rhs == NULL) {   /* unary negation */
                Type ot = resolve_expr(e->lhs);
                if (base_of(ot) != T_INT && base_of(ot) != T_FLOAT)
                    die_at(e->line, "unary `-` needs an int or a float");
                return e->type = ot;   /* -Meters is a Meters */
            }
            if (e->op == TK_TILDE) {   /* unary bitwise NOT: operand in lhs, rhs NULL */
                Type ot = resolve_expr(e->lhs);
                if (ot != T_INT && ot != T_U32 && ot != T_U64)
                    die_at(e->line, "unary `~` needs an int, u32, or u64");
                return e->type = ot;   /* ~u32 is a u32 (32-bit NOT via C `unsigned int`) */
            }
            Type lt = resolve_expr(e->lhs);
            Type rt = resolve_expr(e->rhs);
            if (e->op == TK_AND || e->op == TK_OR) {
                if (lt != T_BOOL || rt != T_BOOL)
                    die_at(e->line, "`%s` needs bool operands", e->op == TK_AND ? "and" : "or");
                return e->type = T_BOOL;
            }
            if (e->op == TK_IN) {              /* `k in m` membership test -> bool */
                if (!is_map(rt))
                    die_at(e->line, "`in` tests membership in a map; the right operand must be a map");
                if (lt != map_key(rt))
                    die_at(e->line, "`in` key must be %s", type_name(map_key(rt)));
                return e->type = T_BOOL;
            }
            /* sized-numeric literal adaptation: an int LITERAL takes the u32/u64 type
             * of the other operand; an int/float LITERAL takes f32. Literals only (a
             * typed variable never changes width), value-directional — mirrors the
             * int->float rule. Lets `w + 1`, `k == 0`, `x & 7` work when w/k/x are
             * u32/u64/f32 without a cast on every constant. */
            if (e->lhs->kind == E_INT && (rt == T_U32 || rt == T_U64)) { e->lhs->type = rt; lt = rt; }
            if (e->rhs->kind == E_INT && (lt == T_U32 || lt == T_U64)) { e->rhs->type = lt; rt = lt; }
            if (rt == T_F32 && (e->lhs->kind == E_INT || e->lhs->kind == E_FLOAT)) {
                if (e->lhs->kind == E_INT) { e->lhs->kind = E_FLOAT; e->lhs->fval = (double)e->lhs->ival; }
                e->lhs->type = T_F32; lt = T_F32;
            }
            if (lt == T_F32 && (e->rhs->kind == E_INT || e->rhs->kind == E_FLOAT)) {
                if (e->rhs->kind == E_INT) { e->rhs->kind = E_FLOAT; e->rhs->fval = (double)e->rhs->ival; }
                e->rhs->type = T_F32; rt = T_F32;
            }
            if (is_cmp(e->op)) {
                if (e->op == TK_EQEQ || e->op == TK_NEQ) {
                    /* equality is structural for every type (value semantics):
                     * ints/bools directly, strings/arrays/structs by content,
                     * recursing through nesting. Only void is incomparable. */
                    if (lt != rt)
                        die_at(e->line, "cannot compare %s with %s", type_name(lt), type_name(rt));
                    if (lt == T_VOID) die_at(e->line, "cannot compare void");
                } else {
                    /* ordering: two ints, two floats, two strings, or two values
                     * of the SAME numeric/string newtype. An int LITERAL adapts to a
                     * float operand (B-1, same as arithmetic — literals only, a
                     * variable never widens), so `f < 0` is `f < 0.0`. */
                    if (lt == T_FLOAT && rt == T_INT && e->rhs->kind == E_INT) {
                        e->rhs->kind = E_FLOAT; e->rhs->fval = (double)e->rhs->ival; e->rhs->type = T_FLOAT; rt = T_FLOAT;
                    } else if (rt == T_FLOAT && lt == T_INT && e->lhs->kind == E_INT) {
                        e->lhs->kind = E_FLOAT; e->lhs->fval = (double)e->lhs->ival; e->lhs->type = T_FLOAT; lt = T_FLOAT;
                    }
                    Type b = base_of(lt);
                    int ok = lt == rt && (b == T_INT || b == T_CHAR || b == T_FLOAT || b == T_STRING ||
                                          b == T_U32 || b == T_U64 || b == T_F32);
                    if (!ok)
                        die_at(e->line, "ordering compares two ints, two floats, two strings, "
                               "or two values of the same numeric newtype");
                }
                return e->type = T_BOOL;
            }
            if (e->op == TK_PLUS && lt == T_STRING) {
                if (rt != T_STRING && rt != T_CHAR)
                    die_at(e->line, "cannot concatenate string with %s", type_name(rt));
                return e->type = T_STRING;   /* string + char appends one byte (no alloc) */
            }
            /* reject division / modulo by a literal zero at compile time
             * (otherwise UB / SIGFPE at runtime). Checks the parsed value so
             * `x / 0` and `x % 0` are caught regardless of literal spelling. */
            if ((e->op == TK_SLASH || e->op == TK_PERCENT) &&
                e->rhs->kind == E_INT && e->rhs->ival == 0)
                die_at(e->line, "division by zero");
            /* shifts: an integer value (int/u32/u64) shifted by any integer count.
             * u32/u64 shift within their C width (`unsigned int`/`unsigned long long`),
             * so `>>` is logical and `<<` wraps — exactly what bit-twiddling wants. */
            if (e->op == TK_SHL || e->op == TK_SHR) {
                int lok = (lt == T_INT || lt == T_U32 || lt == T_U64);
                int rok = (rt == T_INT || rt == T_U32 || rt == T_U64);
                if (!lok || !rok)
                    die_at(e->line, "shift operators require integer operands (got %s, %s)",
                           type_name(lt), type_name(rt));
                return e->type = lt;   /* result width = the shifted value's */
            }
            /* modulo / bitwise: integer-only, both operands the same integer type
             * (int, u32, or u64) — no implicit int/u32 mixing beyond literal adaptation. */
            if (e->op == TK_PERCENT || e->op == TK_AMP || e->op == TK_PIPE || e->op == TK_CARET) {
                if ((lt != T_INT && lt != T_U32 && lt != T_U64) || lt != rt)
                    die_at(e->line, "modulo / bitwise operators require two ints, two u32, or two u64 (got %s, %s)",
                           type_name(lt), type_name(rt));
                return e->type = lt;
            }
            /* arithmetic: two ints or two floats (no implicit mixing — use
             * to_float/to_int). float `/` is true division; int `/` truncates.
             * Two values of the SAME numeric newtype yield that newtype, so units
             * stay typed (Meters + Meters -> Meters) and can't mix with the base. */
            if (lt == T_INT && rt == T_INT) return e->type = T_INT;
            /* sized numerics: `+ - * /` on two same-width values. u32/u64 wrap at their
             * C width (defined, no `-fwrapv` needed for unsigned); f32 is single-precision.
             * No implicit mixing with int/float — literal adaptation above covers constants. */
            if (lt == T_U32 && rt == T_U32) return e->type = T_U32;
            if (lt == T_U64 && rt == T_U64) return e->type = T_U64;
            if (lt == T_F32 && rt == T_F32) return e->type = T_F32;
            /* char arithmetic stays in the byte domain: char±int, int±char, char±char
             * -> char (so `'0' + d` is a char, ready for an in-place string append). */
            if ((e->op == TK_PLUS || e->op == TK_MINUS) &&
                (lt == T_CHAR || rt == T_CHAR) &&
                (lt == T_CHAR || lt == T_INT) && (rt == T_CHAR || rt == T_INT))
                return e->type = T_CHAR;
            if (lt == T_FLOAT && rt == T_FLOAT) return e->type = T_FLOAT;
            if (lt == rt && IS_NEWTYPE(lt) && (nt_under(lt) == T_INT || nt_under(lt) == T_FLOAT))
                return e->type = lt;
            /* B-1: an int LITERAL adapts to the float side (value-directional,
             * literals only -- a float never narrows, a variable never widens) */
            if (lt == T_FLOAT && rt == T_INT && e->rhs->kind == E_INT) {
                e->rhs->kind = E_FLOAT; e->rhs->fval = (double)e->rhs->ival; e->rhs->type = T_FLOAT;
                return e->type = T_FLOAT;
            }
            if (rt == T_FLOAT && lt == T_INT && e->lhs->kind == E_INT) {
                e->lhs->kind = E_FLOAT; e->lhs->fval = (double)e->lhs->ival; e->lhs->type = T_FLOAT;
                return e->type = T_FLOAT;
            }
            die_at(e->line, "arithmetic requires two ints or two floats (got %s, %s)",
                   type_name(lt), type_name(rt));
        }
    }
    return T_VOID;
}

/* Resolve `e` where the surrounding context expects type `want`. The only thing
 * this does beyond resolve_expr is fix a bare `None`'s concrete Option type from
 * the context (a decl annotation, return type, assignment target, or param) —
 * the one place None can learn which Option it is. The chosen type is written
 * back onto the E_NONE node so codegen emits the right TychoOpt. */
static Type resolve_exp(Expr *e, Type want) {
    /* Pierce-Turner checking mode (bidirectional inference): a known destination
     * type flows INTO the few expressions that can consume one, before
     * synthesis would have to fail. Dispatch/receivers always synthesize;
     * only literal-ish constructs consume — types stay ground at every line. */
    if (e->kind == E_IDENT && want != T_VOID) {   /* B-3: a pending decl grounds from its first typed use */
        Type vt;
        if (vars_find(e->sval, &vt) && vt == T_PENDING) pend_ground(e->sval, want, e->line);
    }
    if (e->kind == E_ARRLIT && e->nargs == 0 && e->ival == T_VOID) {   /* bare [] (B-0) */
        if (is_array(want) || IS_SOA(want)) e->ival = want;
        else if (is_map(want)) { e->ival = want; e->op = TK_COLON; }
        /* else fall through: resolve_expr reports the no-context error */
    }
    if (e->kind == E_INT && want == T_FLOAT) {   /* int literal adapts to a float context (B-1; literals only) */
        e->kind = E_FLOAT;
        e->fval = (double)e->ival;
        return e->type = T_FLOAT;
    }
    /* sized-numeric literal adapts to a u32/u64/f32 context (a decl annotation,
     * return type, or arg) — `w: u32 = 5`, `return 0u64`, `f(3.0)` where f wants f32. */
    if (e->kind == E_INT && (want == T_U32 || want == T_U64)) return e->type = want;
    if ((e->kind == E_INT || e->kind == E_FLOAT) && want == T_F32) {
        if (e->kind == E_INT) { e->kind = E_FLOAT; e->fval = (double)e->ival; }
        return e->type = T_F32;
    }
    if (e->kind == E_LAMBDA && IS_FUNC(want)) {  /* lambda param/ret elision from the expected fn type (B-2) */
        Proc *pr = g_laminfo[e->ival].proc;
        if (g_laminfo[e->ival].ftype == T_VOID && pr->nparams == func_n(want)) {   /* not yet resolved */
            for (int i = 0; i < pr->nparams; i++)
                if (pr->params[i].type == T_VOID) pr->params[i].type = func_param(want, i);
            if (!pr->has_ret && func_ret(want) != T_VOID) {
                pr->ret = func_ret(want);          /* an elided return becomes the expected one; the */
                pr->body[0]->kind = S_RETURN;      /* body flips from expression-statement to return */
            }
        }
    }
    Type t = resolve_expr(e);
    if (t == T_NONE && IS_OPT(want)) return e->type = want;
    /* Ok(v)/Err(e): the value fixes one of Result's two params; `want` must be a
     * Result whose matching half equals that value's type, and it supplies the
     * other half. The chosen type is written onto the node for codegen. */
    if (t == T_OK_PARTIAL  && IS_RES(want) && res_ok(want)  == e->lhs->type) return e->type = want;
    if (t == T_ERR_PARTIAL && IS_RES(want) && res_err(want) == e->lhs->type) return e->type = want;
    return t;
}

static void resolve_block(Stmt **body, int n, Type ret);

/* ---- CC-3 `parallel for`: chunked fan-out with reduction merge -----------
 * Pure sugar over the CC-1 machinery. The body is lifted to a top-level
 * chunk proc
 *     fn __par<N>(__plo: int, __phi: int, <captures...>) -> <partials>
 * that runs the body over [__plo, __phi) against LOCAL accumulator partials
 * (initialized to the op's identity) and returns them. The site spawns
 * K = tycho_ncpu() chunk tasks -- every capture deep-copied into each task's
 * root arena (copy-in per chunk, the honest thesis cost) -- joins them in
 * chunk order, and folds each partial into the real accumulator. The only
 * permitted outer-variable writes are reductions `acc = acc + e` /
 * `acc = acc * e` on int/float locals (incl. the += / *= sugar); everything
 * else the body touches is a value-semantic snapshot, so chunks share zero
 * mutable bytes. Int reductions are exact for any K; float reductions may
 * reassociate (chunked sums), like every parallel-reduce. */
typedef struct {
    int   sig;            /* index into g_sigs of the lifted chunk proc's Sig (also the spawn-site Sig) */
    Proc *proc;           /* the lifted proc (emitted with the lambda procs) */
    Expr *caps[14];       /* resolved E_IDENT reads of the captured outer vars */
    int   ncap;
    const char *accs[4];  /* reduction accumulators (outer int/float vars) */
    TokKind     accop[4]; /* TK_PLUS or TK_STAR */
    Type        acct[4];
    int         accn[4];  /* reduction-statement count per acc (read audit) */
    int   nacc;
    int   spawn_id;       /* index into g_spawn: reuses the CC-1 trampoline emission */
} ParFor;
static ParFor g_parfor[64];
static int g_nparfor = 0;
static ProcVec g_parprocs;   /* lifted chunk procs, emitted with the lambda procs */

static int count_reads_e(Expr *e, const char *nm);
static int count_reads_b(Stmt **body, int n, const char *nm);

/* walker state. Nested parallel loops are safe: an outer parfor's walk
 * completes (and is snapshotted into g_parfor) before its lifted body is
 * resolved, which is when an inner parfor re-enters this state fresh. */
static const char *g_pf_locals[128]; static int g_pf_nloc;
static Expr *g_pf_caps[14]; static int g_pf_ncap;
static const char *g_pf_accs[4]; static TokKind g_pf_accop[4];
static Type g_pf_acct[4]; static int g_pf_accn[4]; static int g_pf_nacc;

static int pf_local(const char *n) {
    for (int i = 0; i < g_pf_nloc; i++) if (!strcmp(g_pf_locals[i], n)) return 1;
    return 0;
}
static void pf_add_local(const char *n) {
    if (g_pf_nloc >= 128) { fprintf(stderr, "tychoc: parallel for body declares too many locals\n"); exit(1); }
    g_pf_locals[g_pf_nloc++] = n;
}
static void pf_capture(Expr *id) {
    for (int i = 0; i < g_pf_ncap; i++) if (!strcmp(g_pf_caps[i]->sval, id->sval)) return;
    if (g_pf_ncap >= 14) die_at(id->line, "parallel for captures at most 14 outer variables");
    g_pf_caps[g_pf_ncap++] = id;
}
static void pf_scan_expr(Expr *e) {
    if (!e) return;
    if (e->kind == E_ORRETURN)
        die_at(e->line, "or_return cannot cross a parallel for (no early exit from a chunk)");
    if (e->kind == E_ADDR) {
        Expr *root = e->lhs;
        while (root && (root->kind == E_FIELD || root->kind == E_INDEX || root->kind == E_TUPIDX))
            root = root->lhs;
        if (root && root->kind == E_IDENT && !pf_local(root->sval))
            die_at(e->line, "parallel for cannot pass a captured variable as mut (no shared mutation across chunks)");
    }
    if (e->kind == E_IDENT) {
        Type vt;
        if (!pf_local(e->sval) && vars_find(e->sval, &vt)) {
            if (IS_TASK(vt)) die_at(e->line, "a parallel for cannot capture a task handle -- wait it first");
            if (IS_HANDLE(vt)) die_at(e->line, "a parallel for cannot capture a handle -- it is freed at the end of its scope");
            pf_capture(e);
        }
        return;
    }
    if (e->kind == E_LAMBDA) {   /* a lambda body's outer reads must become chunk-proc params too */
        LamInfo *li = &g_laminfo[e->ival];
        int save = g_pf_nloc;
        for (int i = 0; i < li->proc->nparams; i++) pf_add_local(li->proc->params[i].name);
        pf_scan_expr(li->proc->body[0]->expr);
        g_pf_nloc = save;
        return;
    }
    pf_scan_expr(e->lhs); pf_scan_expr(e->rhs);
    for (int i = 0; i < e->nargs; i++) pf_scan_expr(e->args[i]);
}
static void pf_scan_stmt(Stmt *s, int loopdepth);
static void pf_scan_body(Stmt **body, int n, int loopdepth) {
    int save = g_pf_nloc;
    for (int i = 0; i < n; i++) pf_scan_stmt(body[i], loopdepth);
    g_pf_nloc = save;   /* block locals go out of scope */
}
static void pf_scan_stmt(Stmt *s, int loopdepth) {
    switch (s->kind) {
        case S_RETURN:
            die_at(s->line, "return cannot cross a parallel for");
        case S_BREAK:
            if (loopdepth == 0)
                die_at(s->line, "break cannot apply to a parallel for (chunks cannot stop each other)");
            return;
        case S_CONTINUE: return;
        case S_CONST: pf_add_local(s->name); return;   /* folded at use; track the name so a use isn't flagged as captured */
        case S_DECL:
            if (s->ctrl) { pf_add_local(s->name); pf_scan_stmt(s->ctrl, loopdepth); return; }   /* value if/match decl: loop-local, scan branch tails */
            pf_scan_expr(s->expr); pf_add_local(s->name); return;
        case S_MDECL:
            pf_scan_expr(s->expr);
            for (int i = 0; i < s->nnames; i++) pf_add_local(s->names[i]);
            return;
        case S_MASSIGN:
            pf_scan_expr(s->expr);
            for (int i = 0; i < s->nnames; i++)
                if (!pf_local(s->names[i]))
                    die_at(s->line, "parallel for cannot assign to captured variable '%s'", s->names[i]);
            return;
        case S_ASSIGN: {
            if (pf_local(s->name)) { pf_scan_expr(s->expr); return; }
            Type vt;
            if (!vars_find(s->name, &vt))
                die_at(s->line, "assignment to unknown variable '%s'", s->name);
            Expr *e = s->expr;
            int red = (vt == T_INT || vt == T_FLOAT) && e->kind == E_BINOP
                && (e->op == TK_PLUS || e->op == TK_STAR)
                && e->lhs->kind == E_IDENT && !strcmp(e->lhs->sval, s->name)
                && count_reads_e(e->rhs, s->name) == 0;
            if (!red)
                die_at(s->line, "parallel for may update an outer variable only as a reduction: "
                       "%s = %s + e or %s = %s * e (int/float)", s->name, s->name, s->name, s->name);
            int ai = -1;
            for (int i = 0; i < g_pf_nacc; i++) if (!strcmp(g_pf_accs[i], s->name)) ai = i;
            if (ai < 0) {
                if (g_pf_nacc >= 4) die_at(s->line, "parallel for supports at most 4 reduction accumulators");
                ai = g_pf_nacc++;
                g_pf_accs[ai] = s->name; g_pf_accop[ai] = e->op;
                g_pf_acct[ai] = vt; g_pf_accn[ai] = 0;
            } else if (g_pf_accop[ai] != e->op) {
                die_at(s->line, "accumulator '%s' must use one reduction op consistently", s->name);
            }
            g_pf_accn[ai]++;
            pf_scan_expr(e->rhs);   /* the lhs read IS the reduction; only the rest is scanned */
            return;
        }
        case S_INDEXSET: case S_FIELDSET: {
            Expr *root = s->target;
            while (root && (root->kind == E_FIELD || root->kind == E_INDEX || root->kind == E_TUPIDX))
                root = root->lhs;
            const char *rn = (root && root->kind == E_IDENT) ? root->sval : s->name;
            if (rn && !pf_local(rn))
                die_at(s->line, "parallel for cannot mutate captured variable '%s' in place", rn);
            pf_scan_expr(s->target); pf_scan_expr(s->expr);
            return;
        }
        case S_IF:
            pf_scan_expr(s->expr);
            pf_scan_body(s->body, s->nbody, loopdepth);
            pf_scan_body(s->els, s->nels, loopdepth);
            return;
        case S_WHILE:
            pf_scan_expr(s->expr);
            pf_scan_body(s->body, s->nbody, loopdepth + 1);
            return;
        case S_FORRANGE: {   /* incl. a nested parallel for: walk it like a loop; it lifts itself later */
            pf_scan_expr(s->r_start); pf_scan_expr(s->r_stop); pf_scan_expr(s->r_step);
            int save = g_pf_nloc;
            pf_add_local(s->name);
            pf_scan_body(s->body, s->nbody, loopdepth + 1);
            g_pf_nloc = save;
            return;
        }
        case S_MATCH: {
            pf_scan_expr(s->expr);
            for (int a = 0; a < s->narms; a++) {
                int save = g_pf_nloc;
                for (int b = 0; b < s->arms[a].nbinds; b++) pf_add_local(s->arms[a].binds[b]);
                pf_scan_body(s->arms[a].body, s->arms[a].nbody, loopdepth);
                g_pf_nloc = save;
            }
            return;
        }
        case S_EXPR:
            pf_scan_expr(s->expr);
            return;
        case S_SELECT: {
            /* A channel `select` inside a parallel-for chunk: each arm's channel
             * expr is scanned in the enclosing scope (so the channel becomes a
             * by-value capture -- a shared handle, NOT deep-copied, since a
             * Channel is scalar; multiple chunks legitimately share the queue),
             * each arm's bind names are loop-local, and each arm body is scanned
             * like a match arm so the existing gates still fire (return/break
             * cannot cross, no mut-capture, reductions only). */
            for (int a = 0; a < s->narms; a++) {
                if (s->sel_ch && s->sel_ch[a]) pf_scan_expr(s->sel_ch[a]);
                int save = g_pf_nloc;
                for (int b = 0; b < s->arms[a].nbinds; b++) pf_add_local(s->arms[a].binds[b]);
                pf_scan_body(s->arms[a].body, s->arms[a].nbody, loopdepth);
                g_pf_nloc = save;
            }
            return;
        }
    }
}

static void resolve_parfor(Stmt *s) {
    if (s->foreach) {
        /* `parallel for x in EXPR` (EXPR an identifier, deferred by the parser):
         * type-branch the source into one of two existing forms, then fall
         * through to the normal parfor machinery unchanged. */
        Type src = resolve_exp(s->r_start, T_VOID);
        Expr *coll = s->r_start;          /* the source identifier */
        char *var  = s->name;             /* the user loop variable x */
        Stmt **rb  = s->body; int rn = s->nbody;
        s->foreach = 0;
        if (IS_CHAN(src)) {
            /* K = ncpu() workers, each draining until closed:
             *   parallel for __pw in range(0, ncpu()):
             *       for true: select { recv(coll, x): BODY ; closed: break } */
            Expr *z = new_expr(E_INT, s->line); z->ival = 0;
            Expr *nc = new_expr(E_CALL, s->line); nc->sval = "ncpu"; nc->pkg = "";
            s->name = "__pw"; s->r_start = z; s->r_stop = nc; s->r_step = NULL;
            Stmt *sel = new_stmt(S_SELECT, s->line);
            sel->arms = (MatchArm *)xmalloc(2 * sizeof(MatchArm));
            sel->sel_ch = (Expr **)xmalloc(2 * sizeof(Expr *));
            memset(sel->arms, 0, 2 * sizeof(MatchArm));
            sel->narms = 2;
            sel->arms[0].variant = "recv"; sel->arms[0].nbinds = 1;
            sel->arms[0].binds[0] = var;
            sel->arms[0].body = rb; sel->arms[0].nbody = rn; sel->arms[0].line = s->line;
            sel->sel_ch[0] = coll;
            Stmt *brk = new_stmt(S_BREAK, s->line);
            Stmt **cb = (Stmt **)xmalloc(sizeof(Stmt *)); cb[0] = brk;
            sel->arms[1].variant = "closed"; sel->arms[1].nbinds = 0;
            sel->arms[1].body = cb; sel->arms[1].nbody = 1; sel->arms[1].line = s->line;
            sel->sel_ch[1] = NULL;
            Stmt *whl = new_stmt(S_WHILE, s->line);
            Expr *tru = new_expr(E_BOOL, s->line); tru->ival = 1;
            whl->expr = tru;
            whl->body = (Stmt **)xmalloc(sizeof(Stmt *)); whl->body[0] = sel; whl->nbody = 1;
            s->body = (Stmt **)xmalloc(sizeof(Stmt *)); s->body[0] = whl; s->nbody = 1;
        } else if (is_array(src) || src == T_STRING) {
            /* parallel for __feN in range(0, len(coll)): x := coll[__feN] ; BODY */
            char *iv = sfmt("__fe%d", g_forin_uid++);
            Expr *z = new_expr(E_INT, s->line); z->ival = 0;
            Expr *lc = new_expr(E_CALL, s->line); lc->sval = "len"; lc->pkg = "";
            lc->args = (Expr **)xmalloc(sizeof(Expr *)); lc->args[0] = coll; lc->nargs = 1;
            s->name = iv; s->r_start = z; s->r_stop = lc; s->r_step = NULL;
            Expr *iref = new_expr(E_IDENT, s->line); iref->sval = iv; iref->pkg = "";
            Expr *c2 = new_expr(E_IDENT, s->line); c2->sval = coll->sval; c2->pkg = coll->pkg;
            Expr *idx = new_expr(E_INDEX, s->line); idx->lhs = c2; idx->rhs = iref;
            Stmt *elem = new_stmt(S_DECL, s->line); elem->name = var; elem->expr = idx;
            Stmt **nb = (Stmt **)xmalloc((size_t)(rn + 1) * sizeof(Stmt *));
            nb[0] = elem;
            for (int k = 0; k < rn; k++) nb[k + 1] = rb[k];
            s->body = nb; s->nbody = rn + 1;
        } else {
            die_at(s->line, "parallel for expects an array, string, or channel");
        }
    }
    if (s->r_step) die_at(s->line, "parallel for does not support a range step");
    if (resolve_exp(s->r_start, T_INT) != T_INT || resolve_exp(s->r_stop, T_INT) != T_INT)
        die_at(s->line, "parallel for needs an int range");
    if (g_nparfor >= 64) die_at(s->line, "too many parallel for loops (max 64)");
    /* scan: captures, reduction accumulators, fail-closed rejections */
    g_pf_nloc = 0; g_pf_ncap = 0; g_pf_nacc = 0;
    pf_add_local(s->name);
    pf_scan_body(s->body, s->nbody, 0);
    /* an accumulator read anywhere outside its reductions would observe a
     * chunk-local partial, not the global value -- reject */
    for (int i = 0; i < g_pf_nacc; i++) {
        for (int c = 0; c < g_pf_ncap; c++)
            if (!strcmp(g_pf_caps[c]->sval, g_pf_accs[i]))
                die_at(s->line, "reduction accumulator '%s' may only be updated, not read, inside parallel for", g_pf_accs[i]);
        if (count_reads_b(s->body, s->nbody, g_pf_accs[i]) != g_pf_accn[i])
            die_at(s->line, "reduction accumulator '%s' may only be updated, not read, inside parallel for", g_pf_accs[i]);
    }
    /* resolve capture reads in the ENCLOSING scope (sets their types; the
     * spawn site gen_exprs them there, with mut deref handled as usual) */
    for (int i = 0; i < g_pf_ncap; i++) resolve_expr(g_pf_caps[i]);
    int id = g_nparfor++;
    ParFor *pf = &g_parfor[id];
    pf->ncap = g_pf_ncap; pf->nacc = g_pf_nacc;
    for (int i = 0; i < pf->ncap; i++) pf->caps[i] = g_pf_caps[i];
    for (int i = 0; i < pf->nacc; i++) {
        pf->accs[i] = g_pf_accs[i]; pf->accop[i] = g_pf_accop[i];
        pf->acct[i] = g_pf_acct[i]; pf->accn[i] = g_pf_accn[i];
    }
    /* the lifted chunk proc */
    Proc *pr = (Proc *)xmalloc(sizeof(Proc));
    memset(pr, 0, sizeof(Proc));
    pr->name = sfmt("__par%d", id);
    pr->line = s->line;
    pr->nparams = 2 + pf->ncap;
    pr->params = (Param *)xmalloc((size_t)pr->nparams * sizeof(Param));
    pr->params[0] = (Param){ "__plo", T_INT, 0 };
    pr->params[1] = (Param){ "__phi", T_INT, 0 };
    for (int i = 0; i < pf->ncap; i++)
        pr->params[2 + i] = (Param){ pf->caps[i]->sval, pf->caps[i]->type, 0 };
    Type accts[4];
    for (int i = 0; i < pf->nacc; i++) accts[i] = pf->acct[i];
    pr->ret = pf->nacc == 0 ? T_INT : pf->nacc == 1 ? pf->acct[0] : tup_of(accts, pf->nacc);
    pr->has_ret = 1;
    /* body: partial decls (op identity), the chunk loop, return the partials */
    pr->nbody = pf->nacc + 2;
    pr->body = (Stmt **)xmalloc((size_t)pr->nbody * sizeof(Stmt *));
    for (int i = 0; i < pf->nacc; i++) {
        Stmt *d = new_stmt(S_DECL, s->line);
        d->name = (char *)pf->accs[i];
        if (pf->acct[i] == T_FLOAT) {
            Expr *v = new_expr(E_FLOAT, s->line); v->fval = pf->accop[i] == TK_STAR ? 1.0 : 0.0;
            d->expr = v;
        } else {
            Expr *v = new_expr(E_INT, s->line); v->ival = pf->accop[i] == TK_STAR ? 1 : 0;
            d->expr = v;
        }
        pr->body[i] = d;
    }
    Stmt *loop = new_stmt(S_FORRANGE, s->line);
    loop->name = s->name;
    Expr *lo = new_expr(E_IDENT, s->line); lo->sval = "__plo";
    Expr *hi = new_expr(E_IDENT, s->line); hi->sval = "__phi";
    loop->r_start = lo; loop->r_stop = hi; loop->r_step = NULL;
    loop->body = s->body; loop->nbody = s->nbody;
    pr->body[pf->nacc] = loop;
    Stmt *rst = new_stmt(S_RETURN, s->line);
    if (pf->nacc == 0) {
        Expr *z = new_expr(E_INT, s->line); z->ival = 0; rst->expr = z;
    } else if (pf->nacc == 1) {
        Expr *r = new_expr(E_IDENT, s->line); r->sval = (char *)pf->accs[0]; rst->expr = r;
    } else {
        Expr *tp = new_expr(E_TUPLE, s->line);
        tp->args = (Expr **)xmalloc((size_t)pf->nacc * sizeof(Expr *));
        for (int i = 0; i < pf->nacc; i++) {
            Expr *r = new_expr(E_IDENT, s->line); r->sval = (char *)pf->accs[i];
            tp->args[tp->nargs++] = r;
        }
        rst->expr = tp;
    }
    pr->body[pf->nacc + 1] = rst;
    pf->proc = pr;
    /* register the Sig and the spawn site -- the CC-1 trampoline emission
     * (HSpawnA_<sid> + tycho_spawn_<sid>) then serves parallel for verbatim */
    int sg_id = g_nsigs;
    TBL_ENSURE(g_sigs, g_nsigs, g_sigs_cap);
    Sig *sg = &g_sigs[g_nsigs++];
    memset(sg, 0, sizeof(Sig));
    sg->name = pr->name; sg->ret = pr->ret; sg->nparams = pr->nparams;
    for (int i = 0; i < pr->nparams; i++) sg->params[i] = pr->params[i].type;
    pf->sig = sg_id;
    TBL_ENSURE(g_spawn, g_nspawn, g_spawn_cap);
    pf->spawn_id = g_nspawn;
    g_spawn[g_nspawn++] = sg_id;
    s->par_id = id;
    /* resolve the lifted body (params shadow the enclosing originals) */
    int mark = vars_mark();
    for (int i = 0; i < pr->nparams; i++) {
        Type pt = pr->params[i].type;
        vars_push(pr->params[i].name, pt, !is_array(pt) && !is_map(pt) && !IS_SOA(pt));
    }
    Type saved = g_fn_ret; g_fn_ret = pr->ret;
    g_dup_base = mark;   /* parallel-for body shares its params scope (same lifted C function) */
    resolve_block(pr->body, pr->nbody, pr->ret);
    g_fn_ret = saved;
    vars_restore(mark);
    if (g_parprocs.n == g_parprocs.cap) {
        g_parprocs.cap = g_parprocs.cap ? g_parprocs.cap * 2 : 8;
        g_parprocs.v = (Proc **)xrealloc(g_parprocs.v, (size_t)g_parprocs.cap * sizeof(Proc *));
    }
    g_parprocs.v[g_parprocs.n++] = pr;
}

/* --- loop-progress warning -------------------------------------------------
 * Flag a `for <cond>:` (while-form) that can't make progress: a real comparison
 * whose variables are never changed in the body, with no break/return/die to end
 * it. SOUND because value semantics means a variable changes only by assignment
 * to it, a place-mutation of it (v[i]=, v.f=), or being passed by `&` (mut) --
 * there is no aliasing, so a body that does none of those to a condition variable
 * truly cannot move it. `for true:` (a constant condition, no variables) and
 * call-bearing conditions (e.g. `for len(q) > 0:`) are deliberately skipped. */
/* Large enough to hold every mutated name in a big loop body (e.g. the sha256 /
 * md5 block loops have dozens) -- if the table still overflows, wl_check treats
 * that as "can't prove no progress" and stays silent, so it never false-fires. */
#define WL_MAX 256

static void wl_cond_vars(Expr *e, const char *v[], int *n, int *has_call) {
    if (!e) return;
    if (e->kind == E_CALL || e->kind == E_SPAWN) { *has_call = 1; return; }
    if (e->kind == E_IDENT) { if (*n < WL_MAX) v[(*n)++] = e->sval; return; }
    wl_cond_vars(e->lhs, v, n, has_call);
    wl_cond_vars(e->rhs, v, n, has_call);
    for (int i = 0; i < e->nargs; i++) wl_cond_vars(e->args[i], v, n, has_call);
}

static const char *wl_root(Expr *e) {     /* base identifier of a place: v / v[i] / v.f / v[i].f */
    while (e && (e->kind == E_INDEX || e->kind == E_FIELD)) e = e->lhs;
    return (e && e->kind == E_IDENT) ? e->sval : NULL;
}

static void wl_add(const char *v[], int *n, const char *name) {
    if (name && *n < WL_MAX) v[(*n)++] = name;
}

/* mutations/exits hiding in an expression: &v (mut), push/pop(v,...), die(...) */
static void wl_scan_expr(Expr *e, const char *mut[], int *nm, int *exit) {
    if (!e) return;
    if (e->kind == E_ADDR) wl_add(mut, nm, wl_root(e->lhs));
    if (e->kind == E_CALL && e->sval) {
        if (!strcmp(e->sval, "die")) *exit = 1;
        if ((!strcmp(e->sval, "push") || !strcmp(e->sval, "pop")) && e->nargs >= 1)
            wl_add(mut, nm, wl_root(e->args[0]));
    }
    wl_scan_expr(e->lhs, mut, nm, exit);
    wl_scan_expr(e->rhs, mut, nm, exit);
    for (int i = 0; i < e->nargs; i++) wl_scan_expr(e->args[i], mut, nm, exit);
}

static void wl_scan_body(Stmt **body, int n, const char *mut[], int *nm, int *exit) {
    for (int i = 0; i < n; i++) {
        Stmt *s = body[i];
        if (!s) continue;
        switch (s->kind) {
            case S_ASSIGN: case S_DECL: case S_FORRANGE: wl_add(mut, nm, s->name); break;
            case S_MDECL: case S_MASSIGN:
                for (int j = 0; j < s->nnames; j++) wl_add(mut, nm, s->names[j]);
                break;
            case S_INDEXSET: case S_FIELDSET: wl_add(mut, nm, wl_root(s->target)); break;
            case S_RETURN: case S_BREAK: *exit = 1; break;
            default: break;
        }
        wl_scan_expr(s->expr, mut, nm, exit);
        wl_scan_expr(s->target, mut, nm, exit);
        wl_scan_expr(s->r_start, mut, nm, exit);
        wl_scan_expr(s->r_stop, mut, nm, exit);
        wl_scan_expr(s->r_step, mut, nm, exit);
        wl_scan_body(s->body, s->nbody, mut, nm, exit);
        wl_scan_body(s->els, s->nels, mut, nm, exit);
        for (int a = 0; a < s->narms; a++) wl_scan_body(s->arms[a].body, s->arms[a].nbody, mut, nm, exit);
        if (s->ctrl) wl_scan_body(&s->ctrl, 1, mut, nm, exit);   /* value if/match decl */
    }
}

static void wl_check(Stmt *s) {           /* s is an S_WHILE */
    const char *cv[WL_MAX]; int ncv = 0, has_call = 0;
    wl_cond_vars(s->expr, cv, &ncv, &has_call);
    if (ncv == 0 || has_call) return;     /* constant cond (`for true:`) or a call in it: skip */
    const char *mut[WL_MAX]; int nm = 0, exit = 0;
    wl_scan_body(s->body, s->nbody, mut, &nm, &exit);
    if (exit) return;                     /* a break/return/die can end the loop */
    if (nm >= WL_MAX) return;             /* mut table overflowed -- a mutation may have been dropped, so don't risk a false warning */
    for (int i = 0; i < ncv; i++)
        for (int j = 0; j < nm; j++)
            if (!strcmp(cv[i], mut[j])) return;   /* a condition variable is changed -> progresses */
    warn_at(s->line, "loop condition never changes; this `for` may run forever "
                     "(forgot to advance a variable? consider `for x in range(...)`)");
}

/* "pure" builtins have no side effect, so discarding the result in statement
 * position is always a no-op -- e.g. map_set/map_del return a NEW map (a common
 * footgun: `map_set(m,k,v)` as a bare statement does nothing). */
static int is_pure_builtin(const char *n) {
    if (!n) return 0;
    static const char *pure[] = { "str", "substr", "chr", "split", "keys", "find",
        "map_get", "map_has", "map_set", "map_del", "sqrt", "pow", "floor", "fabs",
        "to_float", "to_int", "to_str", "to_bool", "is_null", "len", 0 };
    for (int i = 0; pure[i]; i++) if (!strcmp(n, pure[i])) return 1;
    return 0;
}

/* >0 while resolving the single-expr tails of a value if/match (see S_EXPR case) */
static int g_value_ctrl = 0;
static void resolve_stmt(Stmt *s, Type ret) {
    switch (s->kind) {
        case S_CONST:   /* local const: register as an immutable literal, scoped like a `:=` (vars_restore drops it at block end); uses fold in resolve_expr */
            vars_push_const(s->name, lit_type(s->expr), s->expr);
            break;
        case S_DECL: {
            if (s->ctrl) {   /* `x := if.../match...` / `x : T = if.../match...` (ROADMAP 2.1) */
                g_value_ctrl++;
                resolve_stmt(s->ctrl, ret);   /* reuse: arm binds, exhaustiveness, per-tail typing */
                g_value_ctrl--;
                Expr *tails[64]; int nt = 0;
                ctrl_collect_tails(s->ctrl, tails, &nt);
                Type t = T_VOID;              /* T_VOID doubles as the "unset" sentinel (a void tail dies first) */
                for (int i = 0; i < nt; i++) {
                    Type ti = tails[i]->type;
                    if (ti == T_NONE || ti == T_OK_PARTIAL || ti == T_ERR_PARTIAL)
                        die_at(tails[i]->line, "cannot infer the type of this branch — annotate the binding (x : T = if/match ...)");
                    if (ti == T_VOID)
                        die_at(tails[i]->line, "a value if/match branch must produce a value, not void");
                    if (t == T_VOID) t = ti;
                    else if (ti != t)
                        die_at(tails[i]->line, "if/match branches produce different types (%s and %s)",
                               type_name(t), type_name(ti));
                }
                if (s->typed_decl) {
                    if (t != s->annot)
                        die_at(s->line, "declared type %s but value is %s", type_name(s->annot), type_name(t));
                    t = s->annot;
                }
                if (IS_TASK(t))
                    die_at(s->line, "a value if/match cannot produce a task handle");
                s->decl_type = t;
                vars_push(s->name, t, 1);
                ctrl_rewrite_tails(s->ctrl, S_ASSIGN, s->name, NULL);   /* tails become `name = tail` */
                break;
            }
            /* channel creation is legal exactly here (CC-4); the marker lets
             * the channel(...) resolve case reject every other position */
            if (s->expr->kind == E_CALL && s->expr->sval && !strcmp(s->expr->sval, "channel") && !s->expr->qual)
                s->expr->op = TK_COLONEQ;
            /* B-3 (bidirectional inference): an UNTYPED decl from a bare [] / None
             * defers -- T_PENDING until the first grounding use in this block
             * (resolve_block audits the leftovers). Checked BEFORE resolution:
             * resolving the bare initializer itself would fail. */
            if (!s->typed_decl
                && ((s->expr->kind == E_ARRLIT && s->expr->nargs == 0 && s->expr->ival == T_VOID)
                    || s->expr->kind == E_NONE)) {
                if (g_npend >= 32) die_at(s->line, "too many pending declarations in one function");
                g_pend[g_npend].name = s->name; g_pend[g_npend].decl = s; g_pend[g_npend].done = 0; g_npend++;
                s->decl_type = T_PENDING;
                vars_push(s->name, T_PENDING, 1);
                break;
            }
            Type t = s->typed_decl ? resolve_exp(s->expr, s->annot) : resolve_expr(s->expr);
            if (t == T_VOID) die_at(s->line, "cannot bind a void value");
            if (s->typed_decl) {
                if (t != s->annot)
                    die_at(s->line, "declared type %s but value is %s",
                           type_name(s->annot), type_name(t));
                t = s->annot;
            } else if (t == T_NONE) {
                die_at(s->line, "cannot infer the type of None — annotate it (x : Option(T) = None)");
            } else if (t == T_OK_PARTIAL || t == T_ERR_PARTIAL) {
                die_at(s->line, "cannot infer the Result type of %s — annotate it "
                       "(x : Result(T, E) = %s)", t == T_OK_PARTIAL ? "Ok(...)" : "Err(...)",
                       t == T_OK_PARTIAL ? "Ok(...)" : "Err(...)");
            }
            /* CC-2 affine tasks: a handle is born from spawn and dies at its
             * one wait (or the scope-exit implicit join). Binding an existing
             * task to a second name would alias it -> two waits possible. */
            if (IS_TASK(t) && s->expr->kind != E_SPAWN)
                die_at(s->line, "a task handle cannot be copied or re-bound -- bind the spawn directly (t := spawn f(...))");
            s->decl_type = t;
            vars_push(s->name, t, 1);
            break;
        }
        case S_MDECL: {   /* a, b := f() — destructure a tuple into fresh locals */
            Type rt = resolve_expr(s->expr);
            if (!IS_TUP(rt))
                die_at(s->line, "the right side of a destructuring `:=` must be a tuple, got %s",
                       type_name(rt));
            if (tup_n(rt) != s->nnames)
                die_at(s->line, "destructuring %d name(s) from a %d-element tuple",
                       s->nnames, tup_n(rt));
            for (int i = 0; i < s->nnames; i++) {
                for (int j = 0; j < i; j++)
                    if (!strcmp(s->names[i], s->names[j]))
                        die_at(s->line, "duplicate name '%s' in the destructuring list", s->names[i]);
                s->mtypes[i] = tup_elem(rt, i);
                vars_push(s->names[i], s->mtypes[i], 1);
            }
            break;
        }
        case S_MASSIGN: {   /* a, b = f() — assign a tuple's elements to EXISTING vars */
            Type rt = resolve_expr(s->expr);
            if (!IS_TUP(rt))
                die_at(s->line, "the right side of a multi-assign must be a tuple, got %s", type_name(rt));
            if (tup_n(rt) != s->nnames)
                die_at(s->line, "assigning %d name(s) from a %d-element tuple", s->nnames, tup_n(rt));
            for (int i = 0; i < s->nnames; i++) {
                Type vt;
                if (!vars_find(s->names[i], &vt)) {
                    const char *sg = suggest_var(s->names[i]);
                    if (sg) die_at(s->line, "assignment to unknown variable '%s' (use ':=' to declare); did you mean '%s'?", s->names[i], sg);
                    die_at(s->line, "assignment to unknown variable '%s' (use ':=' to declare)", s->names[i]);
                }
                s->mtypes[i] = tup_elem(rt, i);
                if (s->mtypes[i] != vt)
                    die_at(s->line, "cannot assign %s to '%s' of type %s",
                           type_name(s->mtypes[i]), s->names[i], type_name(vt));
            }
            break;
        }
        case S_ASSIGN: {
            { Var *cv = vars_lookup(s->name);   /* const is immutable (local const, or top-level when no local shadows it) */
              if (cv ? (cv->lit != NULL) : (consts_find(s->name) != NULL))
                  die_at(s->line, "cannot assign to constant '%s'", s->name); }
            Type vt;
            if (!vars_find(s->name, &vt)) {
                const char *sg = suggest_var(s->name);
                if (sg) die_at(s->line, "assignment to unknown variable '%s'; did you mean '%s'?", s->name, sg);
                die_at(s->line, "assignment to unknown variable '%s'", s->name);
            }
            if (IS_TASK(vt))   /* CC-2: rebinding would orphan the running task (or alias another) */
                die_at(s->line, "a task variable cannot be reassigned -- each task is waited exactly once");
            if (IS_CHAN(vt))   /* CC-4: rebinding would orphan the created channel (freed once, at its decl's scope exit) */
                die_at(s->line, "a channel variable cannot be reassigned");
            if (IS_HANDLE(vt))   /* FFI R2: rebinding would orphan the old handle (its scope-exit free targets one value) */
                die_at(s->line, "a handle variable cannot be reassigned -- it is freed once, at the end of its scope");
            if (vt == T_PENDING) {   /* B-3: the first assignment grounds a pending decl */
                Type gt = resolve_expr(s->expr);
                pend_ground(s->name, gt, s->line);
                break;
            }
            Type t = resolve_exp(s->expr, vt);
            if (t != vt)
                die_at(s->line, "cannot assign %s to '%s' of type %s",
                       type_name(t), s->name, type_name(vt));
            break;
        }
        case S_RETURN: {
            if (s->expr) {
                Type t = resolve_exp(s->expr, ret);
                if (ret == T_VOID) die_at(s->line, "this proc returns nothing");
                if (t != ret)
                    die_at(s->line, "returning %s but proc returns %s",
                           type_name(t), type_name(ret));
                /* A closure MAY escape: S_RETURN codegen re-homes its captured env
                 * into the caller's arena via the closure's copyenv thunk (value
                 * semantics, sound with no lifetime annotations). A plain reference
                 * has copyenv=0 (nothing to re-home). */
            } else if (ret != T_VOID) {
                die_at(s->line, "missing return value (proc returns %s)", type_name(ret));
            }
            break;
        }
        case S_BREAK:
        case S_CONTINUE:
            break;   /* nothing to type-check; outside-a-loop use is caught at codegen */
        case S_IF: {
            if (resolve_expr(s->expr) != T_BOOL)
                die_at(s->line, "if condition must be bool");
            resolve_block(s->body, s->nbody, ret);
            if (s->els) resolve_block(s->els, s->nels, ret);
            break;
        }
        case S_MATCH: {
            Type st = resolve_expr(s->expr);
            /* a `_` wildcard is a catch-all: it must be the last arm, binds nothing,
             * and makes the match exhaustive without listing the remaining variants. */
            int wild = 0;
            for (int i = 0; i < s->narms; i++)
                if (!strcmp(s->arms[i].variant, "_")) {
                    if (i != s->narms - 1) die_at(s->arms[i].line, "a `_` wildcard must be the last match arm");
                    if (s->arms[i].nbinds != 0) die_at(s->arms[i].line, "a `_` wildcard binds nothing");
                    wild = 1;
                }
            if (IS_OPT(st)) {
                Type inner = opt_inner(st);
                int some = 0, none = 0;
                for (int i = 0; i < s->narms; i++) {
                    MatchArm *arm = &s->arms[i];
                    int m = vars_mark();
                    if (!strcmp(arm->variant, "_")) {
                        /* catch-all: no binding */
                    } else if (!strcmp(arm->variant, "Some")) {
                        if (some) die_at(arm->line, "duplicate Some arm");
                        if (arm->nbinds != 1) die_at(arm->line, "Some(x) binds exactly one value");
                        vars_push(arm->binds[0], inner, 1); some = 1;
                    } else if (!strcmp(arm->variant, "None")) {
                        if (none) die_at(arm->line, "duplicate None arm");
                        if (arm->nbinds != 0) die_at(arm->line, "None binds nothing");
                        none = 1;
                    } else {
                        die_at(arm->line, "an Option's arms are Some(x), None, and _, not '%s'", arm->variant);
                    }
                    resolve_block(arm->body, arm->nbody, ret);
                    vars_restore(m);
                }
                if (!wild && (!some || !none)) die_at(s->line, "match on an Option must cover both Some and None");
            } else if (IS_RES(st)) {
                Type okt = res_ok(st), errt = res_err(st);
                int ok = 0, err = 0;
                for (int i = 0; i < s->narms; i++) {
                    MatchArm *arm = &s->arms[i];
                    int m = vars_mark();
                    if (!strcmp(arm->variant, "_")) {
                        /* catch-all: no binding */
                    } else if (!strcmp(arm->variant, "Ok")) {
                        if (ok) die_at(arm->line, "duplicate Ok arm");
                        if (arm->nbinds != 1) die_at(arm->line, "Ok(x) binds exactly one value");
                        vars_push(arm->binds[0], okt, 1); ok = 1;
                    } else if (!strcmp(arm->variant, "Err")) {
                        if (err) die_at(arm->line, "duplicate Err arm");
                        if (arm->nbinds != 1) die_at(arm->line, "Err(e) binds exactly one value");
                        vars_push(arm->binds[0], errt, 1); err = 1;
                    } else {
                        die_at(arm->line, "a Result's arms are Ok(x), Err(e), and _, not '%s'", arm->variant);
                    }
                    resolve_block(arm->body, arm->nbody, ret);
                    vars_restore(m);
                }
                if (!wild && (!ok || !err)) die_at(s->line, "match on a Result must cover both Ok and Err");
            } else if (IS_ENUM(st)) {
                EnumDef *ed = &g_enums[ENUM_ID(st)];
                int *covered = (int *)calloc((size_t)ed->nvariants, sizeof(int));
                for (int i = 0; i < s->narms; i++) {
                    MatchArm *arm = &s->arms[i];
                    if (!strcmp(arm->variant, "_")) {   /* catch-all: no variant, no binding */
                        resolve_block(arm->body, arm->nbody, ret);
                        continue;
                    }
                    int vi = -1;
                    for (int v = 0; v < ed->nvariants; v++)
                        if (!strcmp(ed->variants[v].name, arm->variant)) { vi = v; break; }
                    if (vi < 0) die_at(arm->line, "'%s' is not a variant of %s", arm->variant, ed->name);
                    if (covered[vi]) die_at(arm->line, "duplicate arm for %s", arm->variant);
                    covered[vi] = 1;
                    Variant *var = &ed->variants[vi];
                    if (arm->nbinds != var->npayload)
                        die_at(arm->line, "%s binds %d value(s), got %d", var->name, var->npayload, arm->nbinds);
                    int m = vars_mark();
                    for (int b = 0; b < arm->nbinds; b++) vars_push(arm->binds[b], var->payload[b], 1);
                    resolve_block(arm->body, arm->nbody, ret);
                    vars_restore(m);
                }
                if (!wild)
                    for (int v = 0; v < ed->nvariants; v++)
                        if (!covered[v])
                            die_at(s->line, "non-exhaustive match: missing variant %s of %s",
                                   ed->variants[v].name, ed->name);
                free(covered);
            } else {
                die_at(s->line, "match expects an Option, Result, or enum value, got %s", type_name(st));
            }
            break;
        }
        case S_WHILE: {
            if (resolve_expr(s->expr) != T_BOOL)
                die_at(s->line, "for condition must be bool");
            resolve_block(s->body, s->nbody, ret);
            wl_check(s);
            break;
        }
        case S_SELECT: {   /* CC-5: blocks until a recv arm fires, all channels close+drain, or default */
            int nrecv = 0, ndef = 0, nclosed = 0;
            for (int i = 0; i < s->narms; i++) {
                MatchArm *a = &s->arms[i];
                if (!strcmp(a->variant, "recv")) {
                    nrecv++;
                    Type ct = resolve_expr(s->sel_ch[i]);
                    if (!IS_CHAN(ct))
                        die_at(a->line, "select recv needs a channel, got %s", type_name(ct));
                    int m = vars_mark();
                    vars_push(a->binds[0], chan_inner(ct), 1);
                    resolve_block(a->body, a->nbody, ret);
                    vars_restore(m);
                } else {
                    if (!strcmp(a->variant, "default")) ndef++; else nclosed++;
                    resolve_block(a->body, a->nbody, ret);
                }
            }
            if (nrecv == 0) die_at(s->line, "select needs at least one recv arm");
            if (ndef > 1 || nclosed > 1) die_at(s->line, "select takes at most one default and one closed arm");
            break;
        }
        case S_FORRANGE: {
            if (s->parallel) { resolve_parfor(s); break; }   /* CC-3: body resolves inside the lifted chunk proc */
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
            /* lhs is the array/map being indexed: a variable or a struct's array
             * field (e.g. p.tags[0] = v). The root variable must be mutable. */
            Expr *root = s->target->lhs;
            while (root->kind == E_FIELD || root->kind == E_INDEX) root = root->lhs;
            if (root->kind != E_IDENT)
                die_at(s->line, "can only index-assign an array or map variable or field");
            if (!vars_can_mutate(root->sval))
                die_at(s->line, "cannot mutate parameter '%s' (it is borrowed read-only; copy it with `y := %s` first)",
                       root->sval, root->sval);
            g_place = 1;                          /* m[k] is a legal target here (#2) */
            Type tt = resolve_expr(s->target);    /* E_INDEX -> element/value type */
            g_place = 0;
            if (!is_lvalue(s->target->lhs))       /* the base being indexed must be a place */
                die_at(s->line, "cannot index-assign through this expression (only a variable, field, composite-array element, or map value is a place)");
            Type baset = s->target->lhs->type;    /* the array/map type (set by the resolve above) */
            if (is_map(baset)) {                  /* m[k] = v  or  m[k] op= v  (#2) */
                int compound = (s->expr->kind == E_BINOP && s->expr->lhs == s->target);
                if (compound) {
                    /* read-modify-write on the value slot; restricted to scalar values so the
                     * op lowers to a plain C operator with no arena copy (single-eval'd in codegen). */
                    if (tt != T_INT && tt != T_FLOAT && tt != T_CHAR)
                        die_at(s->line, "compound assignment `m[k] op= ...` is only supported for int/float/char "
                                        "map values; for a composite value use push(m[k], ...) or `m[k] = <expr>`");
                    Type rt = resolve_expr(s->expr->rhs);
                    if (rt != tt)
                        die_at(s->line, "cannot compound-assign %s to a %s map value", type_name(rt), type_name(tt));
                    s->expr->type = tt;           /* the binop yields the value type */
                } else {
                    Type vt = resolve_exp(s->expr, tt);   /* coerces a None value */
                    if (tt != vt)
                        die_at(s->line, "cannot assign %s to a %s map value", type_name(vt), type_name(tt));
                }
                break;
            }
            if (!is_array(baset))
                die_at(s->line, "can only index-assign an array element (strings themselves are immutable)");
            Type vt = resolve_exp(s->expr, tt);   /* coerces a None value */
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
            g_place = 1;                          /* a map index in the chain (m[k].f = v) is a place (#2) */
            Type tt = resolve_expr(s->target);   /* E_FIELD -> field type; also types the chain for is_lvalue */
            g_place = 0;
            if (!is_lvalue(s->target))
                die_at(s->line, "cannot assign to a field of a temporary (only variables, fields, and composite-array elements are places)");
            Type vt = resolve_exp(s->expr, tt);  /* coerces a None value */
            if (tt != vt)
                die_at(s->line, "cannot assign %s to a %s field", type_name(vt), type_name(tt));
            break;
        }
        case S_EXPR: {
            /* inside a value if/match branch (g_value_ctrl), this S_EXPR is the tail
             * VALUE, not a discarded statement — resolve it for its type but skip the
             * discard warning and the discarded-task error. */
            if (!g_value_ctrl && s->expr && s->expr->kind == E_CALL && is_pure_builtin(s->expr->sval))
                warn_at(s->expr->line, "result of `%s` is discarded; it has no side effects, so this statement does "
                                       "nothing (to change a map, use `m[k] = v` or `delete m[k]`)",
                        s->expr->sval);
            Type et = resolve_expr(s->expr);
            if (!g_value_ctrl && IS_TASK(et))   /* CC-2: a discarded handle could never be waited */
                die_at(s->line, "a spawned task must be bound and waited (t := spawn f(...); ... wait(t))");
            break;
        }
    }
}

static void resolve_block(Stmt **body, int n, Type ret) {
    int m = vars_mark();
    int dbase = g_dup_base >= 0 ? g_dup_base : m;   /* fn top block: params included; nested block: own start */
    g_dup_base = -1;
    int pm = g_npend;
    for (int i = 0; i < n; i++) {
        if (body[i]->kind == S_DECL)   /* fail-closed: a same-scope re-`:=` would emit a duplicate C local */
            for (int v = dbase; v < g_nvars; v++)
                if (!strcmp(g_vars[v].name, body[i]->name))
                    die_at(body[i]->line, "'%s' is already declared in this scope", body[i]->name);
        resolve_stmt(body[i], ret);
    }
    for (int i = pm; i < g_npend; i++)   /* B-3 audit: a pending decl must ground in its own block */
        if (!g_pend[i].done)
            die_at(g_pend[i].decl->line, "could not infer the type of '%s' -- no grounding use in its block (annotate: %s : [T] = [] / Option(T) = None)",
                   g_pend[i].name, g_pend[i].name);
    g_npend = pm;
    vars_restore(m);
}

/* A C-identifier-safe spelling of a type, for naming a monomorphic instance
 * (`id__int`, `box__string`). Recurses through containers; a `t<id>` fallback
 * keeps any other type unique. */
static char *type_mangle_ident(Type t) {
    if (t == T_INT)    return "int";
    if (t == T_FLOAT)  return "float";
    if (t == T_STRING) return "string";
    if (t == T_BOOL)   return "bool";
    if (t == T_CHAR)   return "char";
    if (IS_STRUCT(t))  return g_structs[STRUCT_ID(t)].name;
    if (IS_ENUM(t))    return g_enums[ENUM_ID(t)].name;
    if (is_array(t))   return sfmt("arr_%s", type_mangle_ident(arr_elem(t)));
    if (IS_OPT(t))     return sfmt("opt_%s", type_mangle_ident(opt_inner(t)));
    if (IS_RES(t))     return sfmt("res_%s_%s", type_mangle_ident(g_restypes[RES_ID(t)].ok), type_mangle_ident(g_restypes[RES_ID(t)].err));
    if (is_map(t))     return sfmt("map_%s_%s", type_mangle_ident(map_key(t)), type_mangle_ident(map_val(t)));
    return sfmt("t%d", (int)t);
}

/* Instantiate a generic call: infer each `$T` from the matching argument's
 * concrete type, mangle a per-instantiation name, register the instance Sig (so
 * this and later calls resolve), record it for codegen, and rewrite e->sval.
 * Idempotent per (template, concrete arg types). */
/* generics: does type `t` (recursively) mention the specific type parameter `tp`?
 * A `$T` that no parameter mentions is "return-only" -- its binding must be added
 * to the instance name so two explicit instantiations don't collide. */
static int type_mentions_tp(Type t, Type tp) {
    if (t == tp) return 1;
    if (is_array(t)) return type_mentions_tp(arr_elem(t), tp);
    if (IS_OPT(t))  return type_mentions_tp(opt_inner(t), tp);
    if (IS_RES(t))  return type_mentions_tp(g_restypes[RES_ID(t)].ok, tp) || type_mentions_tp(g_restypes[RES_ID(t)].err, tp);
    if (is_map(t))  return type_mentions_tp(map_key(t), tp) || type_mentions_tp(map_val(t), tp);
    return 0;
}

/* generics: the fixed, compiler-known `where` predicate set. Each is exactly the
 * capability the resolver already enforces (base_of sees through newtypes), so a
 * satisfied constraint guarantees the body's op type-checks for that T. */
static int constraint_ok(const char *pred, Type t) {
    Type b = base_of(t);
    if (!strcmp(pred, "numeric"))    return b == T_INT || b == T_FLOAT;                                   /* + - * / */
    if (!strcmp(pred, "comparable")) return b == T_INT || b == T_CHAR || b == T_FLOAT || b == T_STRING;   /* < > <= >= */
    if (!strcmp(pred, "has_str"))    return b == T_INT || b == T_BOOL || b == T_FLOAT || b == T_STRING;   /* str(x) */
    return 1;   /* unknown predicates are rejected at parse */
}

static void instantiate_generic(Proc *gt, Expr *e) {
    if (e->nargs != gt->nparams)
        die_at(e->line, "'%s' takes %d argument(s), got %d", gt->name, gt->nparams, e->nargs);
    Type binds[256];
    for (int i = 0; i < g_ntyparams; i++) binds[i] = T_VOID;   /* T_VOID == unbound */
    if (e->ntypeargs > 0) {   /* explicit call-site type args bind the params in declaration order */
        if (e->ntypeargs != gt->ntyparams)
            die_at(e->line, "'%s' has %d type parameter(s), but %d explicit type argument(s) were given",
                   gt->name, gt->ntyparams, e->ntypeargs);
        for (int i = 0; i < gt->ntyparams; i++)
            binds[(int)(gt->typarams[i] - T_TYPARAM_BASE)] = e->typeargs[i];
    }
    for (int j = 0; j < gt->nparams; j++) {
        Type at_ = resolve_expr(e->args[j]);          /* the concrete argument type */
        /* structurally match the parameter pattern against the arg, binding each
         * `$T` -- handles `$T`, `[$T]`, `Option($T)`, `Result($T,$E)` (Stage 3). */
        if (!match_type(gt->params[j].type, at_, binds))
            die_at(e->line, "argument %d of '%s' is %s, which does not fit the parameter pattern",
                   j + 1, gt->name, type_name(at_));
    }
    /* generics: enforce `where` constraints up front -- a clear signature error
     * instead of a deep "cannot add string and int" inside the substituted body. */
    for (int c = 0; c < gt->ncon; c++) {
        Type ct = binds[(int)(gt->con_tp[c] - T_TYPARAM_BASE)];
        if (ct == T_VOID) continue;
        if (gt->con_nset[c] > 0) {   /* type-set `T: a | b | ...`: ct's base must be one of the listed types */
            int inset = 0;
            for (int j = 0; j < gt->con_nset[c]; j++)
                if (base_of(ct) == base_of(gt->con_set[c][j])) { inset = 1; break; }
            if (!inset) {
                char *setstr = (char *)type_name(gt->con_set[c][0]);
                for (int j = 1; j < gt->con_nset[c]; j++) setstr = sfmt("%s | %s", setstr, type_name(gt->con_set[c][j]));
                die_at(e->line, "'%s' instantiated with %s = %s, which is not in the type set { %s }",
                       gt->name, typaram_name(gt->con_tp[c]), type_name(ct), setstr);
            }
        } else if (!constraint_ok(gt->con_pred[c], ct)) {
            die_at(e->line, "'%s' instantiated with %s = %s, which does not satisfy `%s(%s)`",
                   gt->name, typaram_name(gt->con_tp[c]), type_name(ct), gt->con_pred[c], typaram_name(gt->con_tp[c]));
        }
    }
    /* build the instance's concrete signature (substituting every `$T`) + name */
    char *nm = gt->name;
    Type cparams[16], cret;
    for (int j = 0; j < gt->nparams; j++) {
        cparams[j] = subst_type(gt->params[j].type, binds);
        nm = sfmt("%s__%s", nm, type_mangle_ident(cparams[j]));
    }
    /* a return-only `$T` (no parameter mentions it, e.g. `empty() -> [$T]`) is not
     * captured by the param mangling above; add its binding so `empty$(int)` and
     * `empty$(string)` become distinct instances. */
    for (int i = 0; i < gt->ntyparams; i++) {
        int pinned = 0;
        for (int j = 0; j < gt->nparams; j++)
            if (type_mentions_tp(gt->params[j].type, gt->typarams[i])) { pinned = 1; break; }
        if (!pinned)
            nm = sfmt("%s__%s", nm, type_mangle_ident(binds[(int)(gt->typarams[i] - T_TYPARAM_BASE)]));
    }
    cret = subst_type(gt->ret, binds);
    if (has_typaram(cret))
        die_at(e->line, "the return type of '%s' has a type parameter not fixed by any argument; pass it explicitly, e.g. %s$(int)", gt->name, gt->name);
    e->sval = nm;                                     /* rewrite the call to the instance */
    if (sig_find(nm)) return;                         /* already instantiated */
    Sig s; memset(&s, 0, sizeof s);
    s.name = nm; s.ret = cret; s.nparams = gt->nparams; s.builtin = 0;
    for (int j = 0; j < gt->nparams; j++) { s.params[j] = cparams[j]; s.mut[j] = gt->params[j].is_inout; s.sink[j] = gt->params[j].is_sink; }
    TBL_ENSURE(g_sigs, g_nsigs, g_sigs_cap); g_sigs[g_nsigs++] = s;
    TBL_ENSURE(g_ginsts, g_nginsts, g_nginsts_cap);
    GInst gi; gi.tmpl = gt; gi.name = nm; gi.nparams = gt->nparams; gi.ret = cret;
    gi.binds = (Type *)xmalloc((size_t)(g_ntyparams > 0 ? g_ntyparams : 1) * sizeof(Type));
    for (int i = 0; i < g_ntyparams; i++) gi.binds[i] = binds[i];
    gi.body = clone_block(gt->body, gt->nbody, gi.binds);   /* Stage-2: the instance's own `$T`-substituted body */
    gi.nbody = gt->nbody;
    for (int j = 0; j < gt->nparams; j++) gi.params[j] = cparams[j];
    g_ginsts[g_nginsts++] = gi;
    if (g_nginsts > 1024)   /* runaway guard: fail closed on a recursive generic at a strictly-growing type */
        die_at(e->line, "too many generic instantiations (> 1024) -- a recursive generic at a growing type?");
}

static void resolve_program(ProcVec *prog) {
    register_builtins();
    /* CC-4: a channel handle must not outlive its creating scope. Channel(T)
     * HAS type syntax (unlike Task), so reject the storable positions here. */
    for (int i = 0; i < g_nstructs; i++)
        for (int f = 0; f < g_structs[i].nfields; f++)
            if (IS_CHAN(g_structs[i].fields[f].type))
                die_at(g_structs[i].line, "a struct field cannot be a channel");
    for (int i = 0; i < g_nenums; i++)
        for (int v = 0; v < g_enums[i].nvariants; v++)
            for (int p = 0; p < g_enums[i].variants[v].npayload; p++)
                if (IS_CHAN(g_enums[i].variants[v].payload[p]))
                    die_at(g_enums[i].line, "an enum payload cannot be a channel");
    for (int i = 0; i < g_nnewtypes; i++)
        if (IS_CHAN(g_newtypes[i].under))
            die_at(0, "a newtype cannot wrap a channel");
    /* register every user proc up front so calls can be forward refs */
    for (int i = 0; i < prog->n; i++) {
        Proc *pr = prog->v[i];
        if (pr->generic) {   /* a `$T` template: not a callable Sig -- stash it; instances are made per call */
            if (sig_find(pr->name) || generic_find(pr->name) || consts_find(pr->name))
                die_at(pr->line, "'%s' is already defined", pr->name);
            TBL_ENSURE(g_generics, g_ngenerics, g_generics_cap);
            g_generics[g_ngenerics++] = pr;
            continue;
        }
        if (IS_CHAN(pr->ret))
            die_at(pr->line, "a function cannot return a channel -- create it in the owning scope and pass it down");
        for (int j = 0; j < pr->nparams; j++)
            if (pr->params[j].is_inout && IS_CHAN(pr->params[j].type))
                die_at(pr->line, "a channel parameter cannot be mut (the handle is already shared)");
        if (sig_find(pr->name) || consts_find(pr->name))
            die_at(pr->line, "'%s' is already defined", pr->name);
        Sig s; memset(&s, 0, sizeof s);
        s.name = pr->name; s.ret = pr->ret; s.nparams = pr->nparams; s.builtin = 0;
        s.is_extern = pr->is_extern;
        if (pr->is_extern) add_link(pr->lib);   /* FFI: collect -lLib for the cc line */
        if (pr->nparams > 16) die_at(pr->line, "too many parameters (max 16)");
        for (int j = 0; j < pr->nparams; j++) {
            s.params[j] = pr->params[j].type;
            s.mut[j]  = pr->params[j].is_inout;
            s.sink[j] = pr->params[j].is_sink;
            /* mut: non-heap types (int/bool/pure struct) and the mutable
             * aggregates [int]/[string]/heap-bearing structs. A heap mut
             * carries its value's owning arena (_ina_<name>), so any
             * allocating mutation (element copy, field copy, regrow/push)
             * lands in the caller's arena where the value lives. `string`
             * rides the same machinery: the value itself is immutable, but
             * REASSIGNMENT through the borrow (s = s + ".") reaches the
             * caller, and the new bytes build in _ina_<name>. */
            if (pr->params[j].is_inout && IS_FUNC(pr->params[j].type))
                die_at(pr->line, "mut parameter '%s': a function value can't be mut "
                       "(a callee could write a closure back into the caller and it would dangle)",
                       pr->params[j].name);
        }
        TBL_ENSURE(g_sigs, g_nsigs, g_sigs_cap);
        g_sigs[g_nsigs++] = s;
    }
    Sig *m = sig_find("main");
    if (!m) { fprintf(stderr, "%s: error: no 'main' procedure\n", g_srcname); exit(1); }
    for (int i = 0; i < prog->n; i++) {
        Proc *pr = prog->v[i];
        if (pr->is_extern) continue;   /* FFI: no body to resolve */
        if (pr->generic) continue;     /* generics: the template body is resolved+emitted per instance (gen_program) */
        g_nvars = 0;
        /* arrays ([int]/[string]) are passed as read-only borrows (their
         * buffer is shared, so in-place push/set would hit the caller); all
         * other value params — int/bool/string/struct — are copies and so are
         * mutable locals (a struct field-set rebinds only the local copy). */
        for (int j = 0; j < pr->nparams; j++) {
            Type pt = pr->params[j].type;
            /* arrays, maps, and soa are read-only borrows (they shallow-share
             * the caller's buffers, so in-place mutation would reach through),
             * EXCEPT a mut one, which is a by-pointer share the callee may
             * mutate in place. To mutate a borrowed container, copy it first
             * (`local := param`). */
            int mutable = (!is_array(pt) && !is_map(pt) && !IS_SOA(pt))
                          || pr->params[j].is_inout || pr->params[j].is_sink;
            for (int v = 0; v < g_nvars; v++)   /* fail-closed: a duplicate parameter emits a duplicate C param */
                if (!strcmp(g_vars[v].name, pr->params[j].name))
                    die_at(pr->line, "duplicate parameter '%s'", pr->params[j].name);
            vars_push(pr->params[j].name, pt, mutable);
        }
        if (!strcmp(pr->name, "main") && (pr->nparams != 0 || pr->ret != T_VOID))
            die_at(pr->line, "'main' must be 'fn main():' with no return");
        g_fn_ret = pr->ret;
        g_dup_base = 0;   /* the top body shares the param scope (same C function): a decl colliding with a param is a redeclaration */
        resolve_block(pr->body, pr->nbody, pr->ret);
    }
}

/* ------------------------------------------------------------- codegen */
/* --- bounds-check elision for monotone loop indices -----------------------
 * Inside `for i in range(len(A)):` (start 0, step +1), the access `A[i]` is
 * provably in [0, len(A)): the C loop caches `_stop = len(A)` once at entry,
 * `len` is an un-redefinable builtin returning the true length, Tycho has NO
 * in-place array-shrink op, and we verify below that the loop body never
 * reassigns/shadows A or i and never passes A whole to a call (push / a
 * possibly-mut callee could change it). So the per-element bounds check is
 * redundant and we emit the raw `A.data[i]`. A read `A[i]` and `print(A[i])`,
 * `acc = acc + A[i]` stay elidable (they pass `A[i]`, not `A`); `A = ...`,
 * `push(A, x)`, `f(A)` all disable it. Escape hatch: TYCHOC_NO_BOUNDS_ELISION=1.
 * This is provably-safe range narrowing, NOT a blanket "trust the index". */
typedef struct { const char *iv, *arr; } ElidePair;
static ElidePair g_elide[64];   /* active (loopvar,array) pairs, one per enclosing safe loop */
static int g_nelide;
static int g_elide_disabled = -1;
static int elision_on(void) {
    if (g_elide_disabled < 0) g_elide_disabled = getenv("TYCHOC_NO_BOUNDS_ELISION") ? 1 : 0;
    return !g_elide_disabled;
}

/* Does `e` pass the whole array `arr` (a bare identifier) as a direct argument
 * to any call? Such a call may be mut and shrink/rebind it -> not elidable. */
static int expr_passes_arr(Expr *e, const char *arr) {
    if (!e) return 0;
    if (e->kind == E_CALL)
        for (int i = 0; i < e->nargs; i++)
            if (e->args[i] && e->args[i]->kind == E_IDENT && !strcmp(e->args[i]->sval, arr))
                return 1;
    if (expr_passes_arr(e->lhs, arr) || expr_passes_arr(e->rhs, arr)) return 1;
    for (int i = 0; i < e->nargs; i++) if (expr_passes_arr(e->args[i], arr)) return 1;
    return 0;
}

static int stmts_unsafe(Stmt **body, int n, const char *iv, const char *arr);
/* True if this stmt could invalidate `A[i] in range`: reassign/shadow iv or
 * arr, or pass arr whole to a call (recursively, including nested blocks). */
static int stmt_unsafe(Stmt *s, const char *iv, const char *arr) {
    if (!s) return 0;
    switch (s->kind) {
        case S_DECL: case S_ASSIGN:
            if (s->name && (!strcmp(s->name, iv) || !strcmp(s->name, arr))) return 1;
            break;
        case S_MDECL: case S_MASSIGN:
            for (int i = 0; i < s->nnames; i++)
                if (!strcmp(s->names[i], iv) || !strcmp(s->names[i], arr)) return 1;
            break;
        case S_FORRANGE:   /* a nested loop reusing the name rebinds it */
            if (s->name && (!strcmp(s->name, iv) || !strcmp(s->name, arr))) return 1;
            break;
        default: break;
    }
    if (expr_passes_arr(s->expr, arr) || expr_passes_arr(s->target, arr)) return 1;
    if (expr_passes_arr(s->r_start, arr) || expr_passes_arr(s->r_stop, arr) ||
        expr_passes_arr(s->r_step, arr)) return 1;
    if (stmts_unsafe(s->body, s->nbody, iv, arr)) return 1;
    if (stmts_unsafe(s->els,  s->nels,  iv, arr)) return 1;
    for (int a = 0; a < s->narms; a++) {
        for (int b = 0; b < s->arms[a].nbinds; b++)
            if (!strcmp(s->arms[a].binds[b], iv) || !strcmp(s->arms[a].binds[b], arr)) return 1;
        if (stmts_unsafe(s->arms[a].body, s->arms[a].nbody, iv, arr)) return 1;
    }
    if (s->ctrl && stmt_unsafe(s->ctrl, iv, arr)) return 1;   /* value if/match decl: tails may pass arr to a call */
    return 0;
}
static int stmts_unsafe(Stmt **body, int n, const char *iv, const char *arr) {
    for (int i = 0; i < n; i++) if (stmt_unsafe(body[i], iv, arr)) return 1;
    return 0;
}

/* Is the access base[idx] a loop index proven in-range (so skip the check)? */
static int index_in_range(Expr *base, Expr *idx) {
    if (!elision_on() || base->kind != E_IDENT || idx->kind != E_IDENT) return 0;
    for (int k = g_nelide - 1; k >= 0; k--)
        if (!strcmp(g_elide[k].iv, idx->sval) && !strcmp(g_elide[k].arr, base->sval)) return 1;
    return 0;
}

/* gen_expr returns a freshly allocated C expression string. `arena` is
 * the name of the arena into which any allocation produced by this
 * expression should go (so return values land in the caller's arena). */

static char *gen_expr(Expr *e, const char *arena);
static char *gen_lvalue(Expr *e, const char *arena);   /* C lvalue for a place (with array-element projection) */
static char *return_frees(void);                       /* arena_free()s for every live scope at a return */
static Type g_gen_ret = T_VOID;                        /* return type of the proc being emitted (for or_return) */

static int g_blk = 0;   /* unique-name counter for block subarenas / literals */

/* During codegen we track which arena owns each live variable's storage,
 * so an assignment (or array push) can allocate in the *variable's* arena
 * rather than the current (possibly inner, soon-to-collapse) one. This is
 * what keeps the implicit model sound: a value never outlives its arena. */
typedef struct { const char *name; const char *arena; } CVar;
static CVar *g_cv;
static int  g_ncv = 0, g_cv_cap = 0;
static int  cv_mark(void) { return g_ncv; }
static void cv_restore(int m) { g_ncv = m; }
static void cv_push(const char *name, const char *arena) {
    TBL_ENSURE(g_cv, g_ncv, g_cv_cap);
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

/* names of the current proc's mut params: in the generated body they are
 * C pointers (T *h_x), so every read/lvalue use derefs as (*h_x). Reset per
 * proc; a proc has at most 8 params. */
static const char *g_inout[16];
static int g_ninout = 0;
static int is_inout_param(const char *name) {
    for (int i = 0; i < g_ninout; i++)
        if (!strcmp(g_inout[i], name)) return 1;
    return 0;
}

/* names of ALL the current proc's params. A by-value param is a logical value
 * copy whose underlying heap buffer belongs to the caller (a borrowed
 * array/map/string/struct) or is a _scope deep-copy; either way the codegen
 * must NEVER hand off (move) that buffer to another name, because for a borrowed
 * param the buffer is the CALLER's — a move + later mutation of the destination
 * would corrupt the caller (a value-semantics violation). can_move_from rejects
 * any param. This makes explicit the "NULL = a param" intent the cv_arena scheme
 * documents but no longer enforces (params are tracked as same-arena _scope
 * locals, which otherwise look movable). Reset per proc. */
static const char *g_param[16];
static int g_param_sink[16];   /* parallel: is g_param[i] a `sink` (owned) parameter? */
static int g_nparam = 0;
static int is_param(const char *name) {
    for (int i = 0; i < g_nparam; i++)
        if (!strcmp(g_param[i], name)) return 1;
    return 0;
}
/* is `name` a `sink` parameter — an OWNED value (like a local), consumable once? */
static int is_sink_param(const char *name) {
    for (int i = 0; i < g_nparam; i++)
        if (!strcmp(g_param[i], name)) return g_param_sink[i];
    return 0;
}

/* HEAP mut params additionally carry their value's owning arena as a hidden
 * C parameter `_ina_<name>`. Any allocating mutation of the param (a [string]
 * element copy, a heap struct field copy, an array regrow/push) must allocate
 * into THAT arena — the caller's, where the value lives — not the callee's
 * _scope. Non-heap mut (int/bool/pure struct) never allocates, so it has no
 * arena param. Populated per proc alongside g_inout. */
static const char *g_heap_inout[16];
static int g_nheap_inout = 0;
static int is_heap_inout_param(const char *name) {
    for (int i = 0; i < g_nheap_inout; i++)
        if (!strcmp(g_heap_inout[i], name)) return 1;
    return 0;
}
/* owning-arena C expression for a variable's *root*: the carried _ina_ param
 * for a heap mut, otherwise the variable's tracked arena (cv_arena). */
static char *owner_arena_of(const char *root) {
    if (is_heap_inout_param(root)) return sfmt("_ina_%s", root);
    const char *a = cv_arena(root);
    return (char *)(a ? a : "&_scope");
}

/* --- move-on-last-use (deep-copy elision) -------------------------------
 * `b := a` / `b = a` normally deep-copies a heap value so the two are
 * independent. But if `a` is a uniquely-owned local whose ONLY read is this one,
 * the copy is pure waste — `b` can take over `a`'s buffer (a move). Soundness is
 * conservative and static: (1) the source is a bare variable of heap type; (2) it
 * is read exactly once in the whole function, so this read is its last use on
 * every path, INCLUDING loop back-edges; (3) the move is not lexically inside any
 * loop, so that single textual read is a single dynamic read; (4) the source's
 * arena equals the destination's, so the handed-off buffer's lifetime matches
 * (cv_arena is NULL for parameters, which borrow the caller's buffer — so params
 * are never moved). Like the accumulator reuse, this is FBIP: reuse proven from
 * value semantics + lexical arenas, not reference counts. */
static Stmt **g_proc_body; static int g_proc_nbody;   /* current proc body, for the read-count scan */
static int g_loop_depth = 0;                           /* lexical loop nesting at the current codegen point */
/* The current statement's scope arena. A user function call's ARGUMENTS are
 * transients consumed by the callee — value semantics guarantees the call's
 * return value is freshly owned in _parent and never aliases an argument — so
 * they are allocated here (the innermost loop scratch, reset every iteration)
 * rather than in the result arena, which may live in an outer scope. Set at
 * the top of every gen_stmt; the result arena is still threaded explicitly. */
static const char *g_cur_scope = "&_scope";

static int count_reads_e(Expr *e, const char *nm) {
    if (!e) return 0;
    int c = (e->kind == E_IDENT && e->sval && !strcmp(e->sval, nm)) ? 1 : 0;
    if (e->kind == E_LAMBDA) {   /* the env build reads every captured var at creation
                                  * (e.g. push-loop fusion must not leave `nm` stale) */
        LamInfo *li = &g_laminfo[e->ival];
        for (int i = 0; i < li->ncap; i++)
            if (!strcmp(li->proc->params[i].name, nm)) c++;
    }
    c += count_reads_e(e->lhs, nm) + count_reads_e(e->rhs, nm);
    for (int i = 0; i < e->nargs; i++) c += count_reads_e(e->args[i], nm);
    return c;
}
static int count_reads_b(Stmt **body, int n, const char *nm) {
    int c = 0;
    for (int i = 0; i < n; i++) {
        Stmt *s = body[i];
        c += count_reads_e(s->expr, nm) + count_reads_e(s->target, nm);
        c += count_reads_e(s->r_start, nm) + count_reads_e(s->r_stop, nm) + count_reads_e(s->r_step, nm);
        c += count_reads_b(s->body, s->nbody, nm) + count_reads_b(s->els, s->nels, nm);
        for (int a = 0; a < s->narms; a++) c += count_reads_b(s->arms[a].body, s->arms[a].nbody, nm);
        if (s->ctrl) c += count_reads_b(&s->ctrl, 1, nm);   /* value if/match decl: tails read variables too (move-on-last-use correctness) */
        /* S_SELECT: per-arm channel exprs (arm bodies are already in s->arms[].body) */
        if (s->kind == S_SELECT && s->sel_ch)
            for (int a = 0; a < s->narms; a++) c += count_reads_e(s->sel_ch[a], nm);
    }
    return c;
}
static void indent(FILE *o, int n);
/* ---- push-loop fusion ----------------------------------------------------
 * A loop whose body only pushes to a local scalar array pays, per element, for
 * the descriptor (data/len/cap) going through memory: the C compiler must assume
 * `&arr` aliases the arena pointer also passed to push, so it cannot keep the
 * cursor in registers. Fusion caches data/len/cap in C locals across the loop
 * (register-resident hot path: `_fd[_fl++] = v`), calling a grow hook only on
 * overflow, and writes the descriptor back at loop exit. ~3.7x on push-heavy
 * loops. Sound by construction: fuse ONLY when the array is used solely as
 * push(arr,...) in the body (count_reads == pushcount), is a plain local
 * declared OUTSIDE the loop (not a param, not reassigned/shadowed inside), and
 * holds scalar elements. break/continue need nothing (the flush sits after the
 * loop, which break falls through to and continue's register cursor survives);
 * return flushes via the registry before it leaves. */
static int expr_pushcount(Expr *e, const char *nm) {
    if (!e) return 0;
    int c = (e->kind == E_CALL && e->sval && !strcmp(e->sval, "push") && e->nargs >= 1
             && e->args[0]->kind == E_IDENT && e->args[0]->sval
             && !strcmp(e->args[0]->sval, nm)) ? 1 : 0;
    c += expr_pushcount(e->lhs, nm) + expr_pushcount(e->rhs, nm);
    for (int i = 0; i < e->nargs; i++) c += expr_pushcount(e->args[i], nm);
    return c;
}
static int body_pushcount(Stmt **body, int n, const char *nm) {
    int c = 0;
    for (int i = 0; i < n; i++) {
        Stmt *s = body[i];
        c += expr_pushcount(s->expr, nm) + expr_pushcount(s->target, nm);
        c += expr_pushcount(s->r_start, nm) + expr_pushcount(s->r_stop, nm) + expr_pushcount(s->r_step, nm);
        c += body_pushcount(s->body, s->nbody, nm) + body_pushcount(s->els, s->nels, nm);
        for (int a = 0; a < s->narms; a++) c += body_pushcount(s->arms[a].body, s->arms[a].nbody, nm);
        if (s->ctrl) c += body_pushcount(&s->ctrl, 1, nm);   /* value if/match decl */
    }
    return c;
}
/* does any stmt define/shadow `nm` (so the body's `nm` isn't the array we cache)? */
static int body_defines(Stmt **body, int n, const char *nm) {
    for (int i = 0; i < n; i++) {
        Stmt *s = body[i];
        if ((s->kind == S_DECL || s->kind == S_ASSIGN || s->kind == S_FORRANGE)
            && s->name && !strcmp(s->name, nm)) return 1;
        if (s->kind == S_MDECL || s->kind == S_MASSIGN)
            for (int k = 0; k < s->nnames; k++) if (!strcmp(s->names[k], nm)) return 1;
        for (int a = 0; a < s->narms; a++) {
            for (int b = 0; b < s->arms[a].nbinds; b++) if (!strcmp(s->arms[a].binds[b], nm)) return 1;
            if (body_defines(s->arms[a].body, s->arms[a].nbody, nm)) return 1;
        }
        if (body_defines(s->body, s->nbody, nm)) return 1;
        if (body_defines(s->els, s->nels, nm)) return 1;
        if (s->ctrl && body_defines(&s->ctrl, 1, nm)) return 1;   /* value if/match decl (its s->name is already checked above) */
    }
    return 0;
}
/* gather distinct scalar arrays pushed anywhere in `body` (E_IDENT first arg) */
static void fuse_gather(Stmt **body, int n, const char **names, Type *tys, int *cnt) {
    for (int i = 0; i < n; i++) {
        Stmt *s = body[i];
        Expr *e = s->expr;
        if (e && e->kind == E_CALL && e->sval && !strcmp(e->sval, "push") && e->nargs >= 1
            && e->args[0]->kind == E_IDENT && e->args[0]->sval
            && is_array(e->args[0]->type)) {   /* any array element family (int/float/str/composite); soa + mut excluded by is_array */
            const char *nm = e->args[0]->sval; int seen = 0;
            for (int k = 0; k < *cnt; k++) if (!strcmp(names[k], nm)) { seen = 1; break; }
            if (!seen && *cnt < 16) { names[*cnt] = nm; tys[*cnt] = e->args[0]->type; (*cnt)++; }
        }
        fuse_gather(s->body, s->nbody, names, tys, cnt);
        fuse_gather(s->els, s->nels, names, tys, cnt);
        for (int a = 0; a < s->narms; a++) fuse_gather(s->arms[a].body, s->arms[a].nbody, names, tys, cnt);
        if (s->ctrl) fuse_gather(&s->ctrl, 1, names, tys, cnt);   /* value if/match decl */
    }
}
static struct { const char *arr; int id; Type ty; } g_fuse[16];
static int g_nfuse = 0;
static int fuse_idx(const char *nm) {
    for (int i = g_nfuse - 1; i >= 0; i--) if (!strcmp(g_fuse[i].arr, nm)) return i;
    return -1;
}
/* detect + emit cursor decls for `body`'s fusible arrays; returns count opened.
 * `guard` is a per-iteration control expr (a while condition) that must not read
 * the array -- it would see the stale descriptor; NULL for a for-range (its
 * bounds are evaluated once, before the cursor diverges). */
static int fuse_open(FILE *o, Stmt **body, int n, int ind, Expr *guard) {
    const char *names[16]; Type tys[16]; int cnt = 0, opened = 0;
    fuse_gather(body, n, names, tys, &cnt);
    for (int i = 0; i < cnt; i++) {
        const char *nm = names[i];
        if (g_nfuse >= 16) break;                        /* table full (nested loops share it) -> skip fusing, plain pushes stay correct */
        if (fuse_idx(nm) >= 0) continue;                 /* an enclosing loop already cached it */
        if (!cv_arena(nm) || is_param(nm)) continue;     /* must be a plain local (h_nm is a value) */
        if (body_defines(body, n, nm)) continue;         /* reassigned/shadowed -> not a stable cursor */
        if (count_reads_b(body, n, nm) != body_pushcount(body, n, nm)) continue;  /* used beyond push */
        if (guard && count_reads_e(guard, nm)) continue; /* loop condition reads it -> stale view */
        int id = g_blk++;
        indent(o, ind);
        fprintf(o, "%s*_fd%d = h_%s.data; long _fl%d = h_%s.len, _fc%d = h_%s.cap;\n",
                c_type(arr_elem(tys[i])), id, nm, id, nm, id, nm);
        g_fuse[g_nfuse].arr = nm; g_fuse[g_nfuse].id = id; g_fuse[g_nfuse].ty = tys[i];
        g_nfuse++; opened++;
    }
    return opened;
}
/* write one cached cursor back into its descriptor */
static void fuse_flush_one(FILE *o, int ind, int e) {
    indent(o, ind);
    fprintf(o, "h_%s.data = _fd%d; h_%s.len = _fl%d; h_%s.cap = _fc%d;\n",
            g_fuse[e].arr, g_fuse[e].id, g_fuse[e].arr, g_fuse[e].id, g_fuse[e].arr, g_fuse[e].id);
}
/* flush + unregister the last `opened` cursors (after their loop) */
static void fuse_close(FILE *o, int opened, int ind) {
    for (int i = 0; i < opened; i++) fuse_flush_one(o, ind, g_nfuse - 1 - i);
    g_nfuse -= opened;
}

/* may `b := rhs` / `b = rhs` move rhs's buffer instead of deep-copying it, given
 * the destination lives in arena `owner`? See the conditions above. */
static int can_move_from(Expr *rhs, const char *owner) {
    if (rhs->kind != E_IDENT || !type_is_heap(rhs->type)) return 0;
    if (is_param(rhs->sval)) return 0;   /* never move a param's buffer (caller-owned) */
    if (g_loop_depth != 0) return 0;
    const char *a = cv_arena(rhs->sval);
    if (!a || strcmp(a, owner) != 0) return 0;        /* not a same-arena local (NULL = a param) */
    return count_reads_b(g_proc_body, g_proc_nbody, rhs->sval) == 1;
}

/* --- sink-argument adopt (deep-copy elision into a `sink` parameter) -----
 * A `sink` parameter is consumed by the callee, which OWNS and may mutate it.
 * A named local can be ADOPTED (handed off without a copy) under move-on-last-use's
 * conditions EXCEPT the same-arena requirement. Unlike `b := a` — where `b` takes
 * over the buffer and the buffer must therefore live in b's arena — a sink CALL only
 * needs (1) the buffer to outlive the call and (2) the in-place mutation to be
 * unobserved. (1) holds for ANY tracked local: locals live in a block/function arena
 * that strictly encloses the per-statement argument scratch (`_t`) the call's args are
 * built in, so the local's buffer always outlives the call. (2) holds when the source
 * is read exactly once outside any loop, so this one read is its last on every path
 * (the same gate as move-on-last-use, minus the arena match). Escape is still a copy:
 * returning the sink param re-homes it to _parent like any value. */
static int can_move_into_sink(Expr *rhs) {
    if (rhs->kind != E_IDENT || !type_is_heap(rhs->type)) return 0;
    if (is_param(rhs->sval)) return 0;          /* a param borrows the caller's buffer — never adopt */
    if (g_loop_depth != 0) return 0;            /* one textual read must be one dynamic read */
    if (!cv_arena(rhs->sval)) return 0;         /* a tracked local: its arena encloses the call scope */
    return count_reads_b(g_proc_body, g_proc_nbody, rhs->sval) == 1;
}

/* --- match-arm payload borrow (deep-copy elision) -----------------------
 * Binding a heap enum payload field (`Add(l, r)` over an Expr) normally
 * deep-copies that field's whole subtree into the arm's arena, so the
 * binding is an independent owned value. But the scrutinee's payload memory
 * already outlives the match (a param borrows the caller's; a local or
 * temporary lives in an enclosing arena), and an enum value is immutable —
 * so the binding can just BORROW the field (share the payload pointer) with
 * no copy, exactly as an array parameter borrows its caller's buffer. The
 * one exception is a binding that is MUTATED in the arm (a [int]/[string]
 * payload that is push'd, element/field-assigned, reassigned, or passed
 * `&`-mut): that write would reach through into the scrutinee and break
 * value semantics, so such a binding keeps its owning copy. Same FBIP reuse
 * as move-on-last-use, applied to destructuring: a match->reconstruct tree
 * rewrite drops from O(n^2) copying to O(n). */
static const char *place_root(Expr *e) {
    while (e && (e->kind == E_FIELD || e->kind == E_INDEX ||
                 e->kind == E_ADDR  || e->kind == E_SLICE)) e = e->lhs;
    return (e && e->kind == E_IDENT) ? e->sval : NULL;
}
static int expr_mutates(Expr *e, const char *nm) {
    if (!e) return 0;
    if (e->kind == E_ADDR) {                  /* &nm... — a mut argument */
        const char *r = place_root(e);
        if (r && !strcmp(r, nm)) return 1;
    }
    if (e->kind == E_CALL && e->op != TK_ENUM /* push(nm..., x) grows nm */
        && e->sval && !strcmp(e->sval, "push") && e->nargs >= 1) {
        const char *r = place_root(e->args[0]);
        if (r && !strcmp(r, nm)) return 1;
    }
    if (expr_mutates(e->lhs, nm) || expr_mutates(e->rhs, nm)) return 1;
    for (int i = 0; i < e->nargs; i++) if (expr_mutates(e->args[i], nm)) return 1;
    return 0;
}
static int block_mutates(Stmt **body, int n, const char *nm) {
    for (int i = 0; i < n; i++) {
        Stmt *s = body[i];
        if (s->kind == S_ASSIGN && s->name && !strcmp(s->name, nm)) return 1;
        if (s->kind == S_INDEXSET || s->kind == S_FIELDSET) {
            const char *r = place_root(s->target);
            if (r && !strcmp(r, nm)) return 1;
        }
        if (expr_mutates(s->expr, nm) || expr_mutates(s->target, nm)) return 1;
        if (expr_mutates(s->r_start, nm) || expr_mutates(s->r_stop, nm)
            || expr_mutates(s->r_step, nm)) return 1;
        if (block_mutates(s->body, s->nbody, nm)) return 1;
        if (block_mutates(s->els, s->nels, nm)) return 1;
        for (int a = 0; a < s->narms; a++)
            if (block_mutates(s->arms[a].body, s->arms[a].nbody, nm)) return 1;
        if (s->ctrl && block_mutates(&s->ctrl, 1, nm)) return 1;   /* value if/match decl: a tail may pass &nm to a mut param */
    }
    return 0;
}

/* (The old strlen-hoist "sidecar" optimization is gone: with length-headered
 * strings, len(s) and bounds-checked s[i] are O(1) directly, so the per-function
 * strlen hoist — which made recursive-descent over a string param O(n^2) — is no
 * longer needed.) */

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
static const char **g_esc;
static int g_esc_cap = 0;
static int g_nesc = 0;
static void collect_escapes(Stmt **body, int n) {
    for (int i = 0; i < n; i++) {
        Stmt *s = body[i];
        if (s->kind == S_RETURN && s->expr && s->expr->kind == E_IDENT) {
            TBL_ENSURE(g_esc, g_nesc, g_esc_cap); g_esc[g_nesc++] = s->expr->sval;
        }
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
static const char **g_accum;
static int g_accum_cap = 0;
static int g_naccum = 0;
/* `acc = acc + e1 + e2 + ... + ek` is a self-append accumulator for ANY k>=1.
 * A left-associative `+` chain on strings parses as (((acc+e1)+e2)+...+ek), so
 * the accumulator is the LEFT-SPINE LEAF. Walk down the `+`/string spine; if
 * the leftmost operand is `acc`, the whole chain is appends onto acc's buffer.
 * (The single-piece k=1 case is just acc+e, the original form.) */
static int is_self_append(Stmt *s) {
    if (s->kind != S_ASSIGN || !s->expr) return 0;
    Expr *e = s->expr;
    if (!(e->kind == E_BINOP && e->op == TK_PLUS && e->type == T_STRING)) return 0;
    for (Expr *cur = e; cur->kind == E_BINOP && cur->op == TK_PLUS && cur->type == T_STRING; cur = cur->lhs)
        if (cur->lhs->kind == E_IDENT && !strcmp(cur->lhs->sval, s->name)) return 1;
    return 0;
}
/* Collect the right operands of a self-append chain (is_self_append(s) true)
 * into `out` in append / source order — (((acc+a)+b)+c) -> [a, b, c]. Returns
 * the operand count, or -1 if it exceeds `max`. */
static int collect_append_ops(Expr *e, const char *name, Expr **out, int max) {
    Expr *rev[64]; int nr = 0;
    for (Expr *cur = e; ; cur = cur->lhs) {
        if (nr >= 64) return -1;
        rev[nr++] = cur->rhs;
        if (cur->lhs->kind == E_IDENT && !strcmp(cur->lhs->sval, name)) break;
    }
    if (nr > max) return -1;
    for (int i = 0; i < nr; i++) out[i] = rev[nr - 1 - i];
    return nr;
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
 * in-place backward-shift delete on m's unique table instead of a pure deep-copy. */
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
        if (is_self_append(s) || is_self_mapset(s) || is_self_mapdel(s)) { TBL_ENSURE(g_accum, g_naccum, g_accum_cap); g_accum[g_naccum++] = s->name; }
        if (s->body) collect_accums(s->body, s->nbody);
        if (s->els)  collect_accums(s->els, s->nels);
        /* match/select arm bodies are nested blocks too (s->arms[a].body, not
         * s->body) -- without this an accumulator inside a `match` arm silently
         * falls back to the pure O(n)-copy map_set/append, turning a channel-
         * drain loop O(n^2). Same soundness as the if/for case: an arm is a
         * scoped block whose binds are fresh locals, never aliases of acc. */
        for (int a = 0; a < s->narms; a++) collect_accums(s->arms[a].body, s->arms[a].nbody);
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
    if (IS_NEWTYPE(t)) t = nt_under(t);   /* re-home as its base (int/float: nothing to do) */
    if (IS_MAPC(t)) return sfmt("tycho_mapc%d_copy(%s, %s)", MAPC_ID(t), arena, val);
    switch (t) {
        case T_STRING:       return sfmt("tycho_str_copy(%s, %s)", arena, val);
        case T_ARRAY_INT:    return sfmt("tycho_arr_int_copy(%s, %s)", arena, val);
        case T_ARRAY_FLOAT:  return sfmt("tycho_arr_float_copy(%s, %s)", arena, val);
        case T_ARRAY_STRING: return sfmt("tycho_arr_str_copy(%s, %s)", arena, val);
        case T_MAP_SI:
        case T_MAP_SF:
        case T_MAP_II:
        case T_MAP_IF:       return sfmt("tycho_map_%s_copy(%s, %s)", map_fn(t), arena, val);
        default:
            if (IS_OPT(t))
                return type_is_heap(t) ? sfmt("tycho_opt%d_copy(%s, %s)", OPT_ID(t), arena, val) : val;
            if (IS_RES(t))
                return type_is_heap(t) ? sfmt("tycho_res%d_copy(%s, %s)", RES_ID(t), arena, val) : val;
            if (IS_TUP(t))
                return type_is_heap(t) ? sfmt("tycho_tup%d_copy(%s, %s)", TUP_ID(t), arena, val) : val;
            if (IS_ENUM(t))
                return type_is_heap(t) ? sfmt("tycho_copy_E_%s(%s, %s)", g_enums[ENUM_ID(t)].name, arena, val) : val;
            if (IS_ARRC(t))
                return sfmt("tycho_arr_C%d_copy(%s, %s)", ARRC_ID(t), arena, val);
            if (IS_FUNC(t))   /* a fn value: re-home its captured env into `arena` (a plain ref has env==0 -> no-op) */
                /* temp is `_fcp`, NOT `_t`: `arena` may be a block arena named `_t`
                 * (e.g. retrieving a closure-valued map entry inside a print block),
                 * and a `_t` temp here would shadow it so copyenv(&_t,...) passes the
                 * FnC* instead of the Arena*. */
                return sfmt("({ FnC%d _fcp = (%s); if (_fcp.env) _fcp.env = _fcp.copyenv(%s, _fcp.env); _fcp; })", FUNC_ID(t), val, arena);
            if (IS_STRUCT(t) && type_is_heap(t))
                return sfmt("tycho_copy_S_%s(%s, %s)", g_structs[STRUCT_ID(t)].name, arena, val);
            if (IS_SOA(t))   /* deep-copy each field buffer into `arena` */
                return sfmt("Soa%d_copy(%s, %s)", SOA_ID(t), arena, val);
            return val;   /* int/bool/float/pure struct: nothing to re-home */
    }
}

/* A "place" expression denotes existing storage (a variable, a field of one,
 * an array element) rather than a freshly-built value. Reading a place only
 * aliases its bytes, so storing a *heap* place into a same-or-longer-lived
 * location must deep-copy. A literal/call/concat/split result is already a
 * fresh value owned by the arena it was built in — no copy needed. */
static int is_place(Expr *e) {
    return e->kind == E_IDENT || e->kind == E_FIELD || e->kind == E_INDEX
        || e->kind == E_TUPIDX || e->kind == E_SLICE;   /* a slice aliases its source; binding it copies */
}

/* A heap return value must be deep-copied into the caller's arena when it READS
 * existing storage (a variable, field, element, tuple slot, or slice) that
 * lives in this scope OR in a by-value parameter's deep-copied arena -- i.e.
 * memory freed when this scope ends. A bare local proven to live in _parent
 * (the return-slot optimization) is exempt; fresh-producing exprs (call, concat,
 * literal, construction) already build in the target arena, so they need no
 * copy. NOTE: field/element reads of a by-value heap-STRUCT param (e.g.
 * `return ctx.field`) alias the callee's copy and MUST be promoted -- the
 * earlier `E_IDENT`-only test missed these (use-after-free; surfaced by the
 * self-hosting tychoc0, whose field_type/sig_ret/resolve_nt return Ctx fields). */
static int ret_must_copy(Expr *e) {
    if (e->kind == E_IDENT) return !cv_in_parent(e->sval);
    return is_place(e);
}

/* --- construction-arg move ----------------------------------------------
 * Materialize `arg` as a heap field/element of a fresh aggregate (an enum
 * payload, Option/Result body, tuple, struct, or array literal) allocated in
 * `arena`. A deep copy is needed ONLY when the arg is an aliasing place
 * (variable/field/element/slice) that is NOT a movable dead local: a fresh
 * temporary already owns its bytes in `arena`, and a uniquely-owned dead
 * local is handed off (the same move as `b := a`, can_move_from). So
 * `t := Pair(a, b)` with `a`/`b` dead stores their buffers directly instead
 * of copying — the FBIP reconstruction reuse on the construction side.
 *
 * g_self_move_name additionally enables a LOOP-CARRIED self-move: in a
 * self-rebuild `t = Pair(t, x)` the old `t` is read once and immediately
 * overwritten, so it is dead at the rebind even inside a loop (the
 * constructor analog of the `acc = acc + e` / `m = map_set(m, ...)` loop
 * accumulators). The single occurrence of the target name is handed off
 * rather than copied, turning an O(n^2) build into O(n). The gate
 * (self_rebuild_move) requires the name to occur exactly once in the RHS and
 * to be a same-arena local, so the move is unique and same-lifetime. */
static const char *g_self_move_name = NULL;
static char *arg_into(Type t, const char *arena, Expr *arg) {
    char *v = gen_expr(arg, arena);
    if (type_is_heap(t) && is_place(arg)) {
        int self_move = g_self_move_name && arg->kind == E_IDENT
            && !strcmp(arg->sval, g_self_move_name);
        if (!self_move && !can_move_from(arg, arena))
            v = copy_into(t, arena, v);
    }
    return v;
}

/* arg_into for a `sink` parameter: a fresh value (non-place) is already owned in
 * `arena`; a place is adopted when can_move_into_sink holds (a dead local in ANY
 * enclosing arena — the arena-placement relaxation), else copied. */
static char *sink_arg_into(Type t, const char *arena, Expr *arg) {
    char *v = gen_expr(arg, arena);
    if (type_is_heap(t) && is_place(arg) && !can_move_into_sink(arg)) {
        /* consume diagnostic: a bare local handed to a `sink` parameter but used
         * again (or inside a loop) cannot be moved into it. Rather than silently
         * copy, require the move-vs-copy to be visible (Hylo-style): the user
         * passes a copy they keep, or makes this the variable's last use. A field/
         * index/param argument still copies (you cannot move a part out of a value). */
        if (arg->kind == E_IDENT && (!is_param(arg->sval) || is_sink_param(arg->sval)))
            die_at(arg->line, "'%s' is consumed by a `sink` parameter but used again (or inside a loop); "
                              "pass a copy you keep (`y := %s`) or make this its last use", arg->sval, arg->sval);
        v = copy_into(t, arena, v);
    }
    return v;
}

/* `t = C(..., t, ...)` — a self-rebuild of a heap aggregate. The old t is read
 * once in the RHS and immediately replaced, so handing off its buffer (rather
 * than deep-copying it) is sound even in a loop. Gate: the target is a tracked
 * same-arena local (not a borrowed/mut param), the RHS is a heap value, and
 * the name occurs EXACTLY once in the RHS — so the moved read is the only use,
 * and nothing else can observe the handed-off buffer. */
static int self_rebuild_move(Stmt *s) {
    const char *nm = s->name;
    if (is_inout_param(nm)) return 0;
    if (!cv_arena(nm)) return 0;
    if (!type_is_heap(s->expr->type)) return 0;
    return count_reads_e(s->expr, nm) == 1;
}

/* C expression that is nonzero iff the two operands of type `t` are equal by
 * *value* — the mirror of copy_into. int/bool compare directly; strings by
 * byte; arrays element-wise; structs field-wise via a generated tycho_eq_S_X.
 * Recurses through nesting exactly as the deep copy does. */
/* deep hash of a value `v` of type t, for composite map keys. Mirrors gen_eq's
 * structure; folds field hashes (the struct body does the fold). int/bool/char ->
 * the seeded SplitMix64 int hash; string/bytes -> keyed SipHash; float -> hash its
 * bit pattern; a fieldless enum -> its tag; a nested struct -> its own hash. Equal
 * values (by deep ==) always hash equal. */
static char *gen_hash(Type t, const char *v) {
    t = base_of(t);
    if (t == T_STRING || t == T_BYTES) return sfmt("tycho_si_hash(%s)", v);
    if (t == T_FLOAT)  return sfmt("tycho_ik_hash((long)((union { double _d; long _l; }){ ._d = (%s) })._l)", v);
    if (enum_fieldless(t)) return sfmt("tycho_ik_hash((long)((%s)->tag))", v);
    if (IS_STRUCT(t))  return sfmt("tycho_hash_S_%s(%s)", g_structs[STRUCT_ID(t)].name, v);
    if (IS_TUP(t))     return sfmt("tycho_hash_T%d(%s)", TUP_ID(t), v);
    if (t == T_ARRAY_INT)    return sfmt("tycho_arr_int_hash(%s)", v);
    if (t == T_ARRAY_STRING) return sfmt("tycho_arr_str_hash(%s)", v);
    if (t == T_ARRAY_FLOAT)  return sfmt("tycho_arr_float_hash(%s)", v);
    if (IS_ARRC(t))    return sfmt("tycho_arr_C%d_hash(%s)", ARRC_ID(t), v);   /* composite-element array: generated, order-sensitive */
    return sfmt("tycho_ik_hash((long)(%s))", v);   /* int / bool / char */
}

static char *gen_eq(Type t, const char *a, const char *b) {
    if (IS_NEWTYPE(t))       return gen_eq(nt_under(t), a, b);
    if (t == T_STRING || t == T_BYTES) return sfmt("(tycho_str_cmp(%s, %s) == 0)", a, b);   /* bytes: byte-wise compare, same buffer repr */
    if (t == T_ARRAY_INT)    return sfmt("tycho_arr_int_eq(%s, %s)", a, b);
    if (t == T_ARRAY_FLOAT)  return sfmt("tycho_arr_float_eq(%s, %s)", a, b);
    if (t == T_ARRAY_STRING) return sfmt("tycho_arr_str_eq(%s, %s)", a, b);
    if (IS_MAPC(t))          return sfmt("tycho_mapc%d_eq(%s, %s)", MAPC_ID(t), a, b);
    if (is_map(t))           return sfmt("tycho_map_%s_eq(%s, %s)", map_fn(t), a, b);
    if (IS_ARRC(t))          return sfmt("tycho_arr_C%d_eq(%s, %s)", ARRC_ID(t), a, b);
    if (IS_ENUM(t))          return sfmt("tycho_eq_E_%s(%s, %s)", g_enums[ENUM_ID(t)].name, a, b);
    if (IS_OPT(t)) {         /* same tag, and equal values when both present */
        Type in = opt_inner(t);
        return sfmt("((%s).has == (%s).has && (!(%s).has || %s))",
                    a, b, a, gen_eq(in, sfmt("(%s).val", a), sfmt("(%s).val", b)));
    }
    if (IS_RES(t)) {         /* same tag, then the active variant's value */
        return sfmt("((%s).ok == (%s).ok && ((%s).ok ? %s : %s))",
                    a, b, a,
                    gen_eq(res_ok(t),  sfmt("(%s).okv",  a), sfmt("(%s).okv",  b)),
                    gen_eq(res_err(t), sfmt("(%s).errv", a), sfmt("(%s).errv", b)));
    }
    if (IS_TUP(t)) {         /* element-wise */
        char *s = sfmt("(%s", gen_eq(tup_elem(t, 0), sfmt("(%s)._0", a), sfmt("(%s)._0", b)));
        for (int i = 1; i < tup_n(t); i++)
            s = sfmt("%s && %s", s, gen_eq(tup_elem(t, i), sfmt("(%s)._%d", a, i), sfmt("(%s)._%d", b, i)));
        return sfmt("%s)", s);
    }
    if (IS_STRUCT(t))        return sfmt("tycho_eq_S_%s(%s, %s)", g_structs[STRUCT_ID(t)].name, a, b);
    if (IS_SOA(t))           return sfmt("Soa%d_eq(%s, %s)", SOA_ID(t), a, b);
    if (IS_FUNC(t))          /* fn values: identity equality (same thunk + same env) — closures aren't structurally comparable */
        return sfmt("((%s).call == (%s).call && (%s).env == (%s).env)", a, b, a, b);
    return sfmt("(%s == %s)", a, b);   /* int/bool/float */
}

/* Drop ONE redundant outer paren layer when a gen_expr result is emitted as an
 * if/while condition: `if ((a == b))` -> `if (a == b)`. gen_expr wraps every
 * binop in parens, and the `if (%s)` / `while (%s)` site adds its own required
 * pair, so an equality condition comes out double-parenthesised -- which clang
 * flags as -Wparentheses-equality (gcc is silent; both accept either form).
 * Strips ONLY when `s` is a single fully-parenthesised group: first char '(' and
 * its matching ')' is the very last char. C string/char literals are skipped, so
 * a paren inside a literal (e.g. `s == ")"`) can't fool the matcher. Fail-closed:
 * on any uncertainty `s` is returned unchanged, so a missed strip is a harmless
 * extra paren, never malformed C. */
static char *cond_unwrap(char *s) {
    if (!s || s[0] != '(') return s;
    int depth = 0;
    for (char *p = s; *p; p++) {
        if (*p == '"' || *p == '\'') {            /* skip a string/char literal */
            char q = *p++;
            while (*p && *p != q) { if (*p == '\\' && p[1]) p++; p++; }
            if (*p != q) return s;                /* unterminated -> bail, unchanged */
            continue;
        }
        if (*p == '(') depth++;
        else if (*p == ')' && --depth == 0) {     /* this ')' matches s[0]'s '(' */
            if (p[1] != '\0') return s;           /* ...but it closes before the end */
            char *out = sfmt("%s", s + 1);        /* drop the leading '(' ... */
            out[strlen(out) - 1] = '\0';          /* ... and the trailing ')' */
            return out;
        }
    }
    return s;                                     /* unbalanced -> unchanged */
}

/* FFI: the bare C call to an extern fn — `name(args)` with NO arena-copy on a
 * string return. The extern branch wraps this in tycho_str_copy for safety; the
 * read-once consumers below (len/print) use it directly, since the C-owned
 * pointer is read immediately and never held. */
static char *gen_extern_raw(Expr *e) {
    /* A `bytes` argument lowers to two C args (const unsigned char* ptr, long len).
     * Bind it to a temp first (single-eval: the arg may be a call). When there are
     * no bytes args, emit the plain `f(args)` unchanged (output-stable FFI). */
    char *decls = sfmt("%s", ""), *args = sfmt("%s", "");
    int emitted = 0, nb = 0;
    for (int i = 0; i < e->nargs; i++) {
        char *a = gen_expr(e->args[i], g_cur_scope);
        if (e->args[i]->type == T_BYTES) {
            char *tv = sfmt("_xb%d", nb++);
            decls = sfmt("%schar *%s = %s; ", decls, tv, a);
            args = sfmt("%s%s(const unsigned char *)%s, tycho_str_len(%s)", args, emitted++ ? ", " : "", tv, tv);
        } else {
            args = sfmt("%s%s%s", args, emitted++ ? ", " : "", a);
        }
    }
    char *call = sfmt("%s(%s)", e->sval, args);
    return decls[0] ? sfmt("({ %s%s; })", decls, call) : call;
}
/* Is e a DIRECT call to an extern fn returning string? (not an indirect/fn-value
 * call, enum/struct/newtype ctor.) Such a result, consumed read-once and inline,
 * needs no arena-copy — the borrow can't escape the consuming call. */
static int is_extern_str_call(Expr *e) {
    if (e->kind != E_CALL || e->lhs || !e->sval || e->op == TK_FN || e->op == TK_ENUM || e->op == TK_TYPE)
        return 0;
    Sig *cs = sig_find(e->sval);
    return cs && cs->is_extern && base_of(cs->ret) == T_STRING;
}

static char *gen_call(Expr *e, const char *arena) {
    if (e->op == TK_TYPE)     /* newtype wrap Meters(x): zero-cost, just the value */
        return gen_expr(e->args[0], arena);
    if (e->op == TK_FN) {     /* indirect call through a function value: g.call(g.env, arena, args) */
        if (e->lhs) {         /* call-on-expression: bind the callee to a temp first (it may have side effects / be an index) */
            char *cv = gen_expr(e->lhs, g_cur_scope);
            char *out = sfmt("({ %s_f = %s; _f.call(_f.env, %s", c_type(e->lhs->type), cv, arena);
            for (int i = 0; i < e->nargs; i++)
                out = sfmt("%s, %s", out, gen_expr(e->args[i], g_cur_scope));
            return sfmt("%s); })", out);
        }
        char *g = sfmt("h_%s", e->sval);
        char *out = sfmt("%s.call(%s.env, %s", g, g, arena);
        for (int i = 0; i < e->nargs; i++)
            out = sfmt("%s, %s", out, gen_expr(e->args[i], g_cur_scope));
        return sfmt("%s)", out);
    }
    if (e->op == TK_ENUM) {   /* enum constructor: descriptor { tag, payload } */
        int eid = ENUM_ID(e->type), vi = (int)e->ival;
        Variant *var = &g_enums[eid].variants[vi];
        const char *en = g_enums[eid].name;
        if (var->npayload == 0)
            return sfmt("(&_sing_%s_%d)", en, vi);   /* shared singleton: no allocation */
        /* arena-allocate one tagged cell sized to THIS variant (tag region + just
         * this variant's payload), not the union max -- a JNum node is 16B, not
         * sizeof(the widest variant). Reads dispatch on the tag, so the smaller
         * cell is never over-read; copies are field-wise (per variant). */
        char *out = sfmt("({ E_%s *_p = (E_%s *)arena_alloc(%s, offsetof(E_%s, u) + sizeof(E_%s_%s)); _p->tag = %d;",
                         en, en, arena, en, en, var->name, vi);
        for (int i = 0; i < var->npayload; i++)
            out = sfmt("%s _p->u.%s.f%d = %s;", out, var->name, i,
                       arg_into(var->payload[i], arena, e->args[i]));
        return sfmt("%s _p; })", out);
    }
    if (!strcmp(e->sval, "len")) {
        if (is_extern_str_call(e->args[0]))   /* FFI: read the length of a C-owned string without copying it (read-once borrow) */
            return sfmt("({ const char *_x = %s; _x ? (long)strlen(_x) : 0L; })", gen_extern_raw(e->args[0]));
        char *a = gen_expr(e->args[0], arena);
        if (e->args[0]->type == T_STRING || e->args[0]->type == T_BYTES)   /* bytes: same length-headered buffer */
            return sfmt("tycho_str_len(%s)", a);
        return sfmt("((%s).len)", a);   /* arrays AND maps both have .len */
    }
    /* map builtins: map_set is pure (deep-copy + insert into `arena`); the
     * accumulator pass rewrites a self-rebind to an in-place put separately. */
    if (!strcmp(e->sval, "map_set")) {
        char *m = gen_expr(e->args[0], arena);
        char *k = key_rt(e->type, gen_expr(e->args[1], arena));
        char *v = gen_expr(e->args[2], arena);   /* runtime put deep-copies v into the map arena */
        return sfmt("%s(%s, %s, %s, %s)", map_rt(e->type, "set"), arena, m, k, v);
    }
    if (!strcmp(e->sval, "map_get")) {
        Type mt = e->args[0]->type;
        char *m = gen_expr(e->args[0], arena);
        char *k = key_rt(mt, gen_expr(e->args[1], arena));
        char *d = gen_expr(e->args[2], arena);
        /* the get returns a BORROW into m's table (or the default); deep-copy it
         * into the current arena so it outlives any later mutation/free of m. For
         * int/float values copy_into is the identity -> byte-identical. */
        char *call = sfmt("%s(%s, %s, %s)", map_rt(mt, "get"), m, k, d);
        return copy_into(map_val(mt), arena, call);
    }
    if (!strcmp(e->sval, "map_has")) {
        char *m = gen_expr(e->args[0], arena);
        char *k = key_rt(e->args[0]->type, gen_expr(e->args[1], arena));
        return sfmt("%s(%s, %s)", map_rt(e->args[0]->type, "has"), m, k);
    }
    /* map_del pure: deep-copy + delete into `arena`; the accumulator pass
     * rewrites a self-rebind to an in-place backward-shift delete separately. */
    if (!strcmp(e->sval, "map_del")) {
        char *m = gen_expr(e->args[0], arena);
        char *k = key_rt(e->type, gen_expr(e->args[1], arena));
        return sfmt("%s(%s, %s, %s)", map_rt(e->type, "del_pure"), arena, m, k);
    }
    if (!strcmp(e->sval, "keys")) {
        char *m = gen_expr(e->args[0], arena);
        return sfmt("%s(%s, %s)", map_rt(e->args[0]->type, "keys"), arena, m);
    }
    if (!strcmp(e->sval, "substr")) {
        char *s = gen_expr(e->args[0], arena);
        char *a = gen_expr(e->args[1], arena);
        char *b = gen_expr(e->args[2], arena);
        return sfmt("tycho_str_substr(%s, %s, %s, %s)", arena, s, a, b);
    }
    if (!strcmp(e->sval, "find")) {
        char *s   = gen_expr(e->args[0], arena);
        char *sub = gen_expr(e->args[1], arena);
        return sfmt("tycho_str_find(%s, %s)", s, sub);
    }
    if (!strcmp(e->sval, "push")) {
        /* grow the array in *its owning arena* (the root variable's), not the
         * current one. The target may be a variable or a struct's array field;
         * &(lvalue) works for both (h_xs / ((h_p).f_tags)). For [string] the
         * runtime push copies the element bytes into that arena, so a pushed
         * loop-scratch temporary does not dangle. */
        if (e->args[0]->kind == E_IDENT) {       /* push-loop fusion: cursor write, no descriptor traffic */
            int fi = fuse_idx(e->args[0]->sval);
            if (fi >= 0) {
                int id = g_fuse[fi].id;
                /* grow-fn + element deep-copy unify across every element family:
                 * arr_fn -> int/float/str/C<id>; copy_into is identity for scalars
                 * (so int/float emission is byte-identical) and the right per-element
                 * deep-copy into the array's arena for str/struct/tuple/ARRC/... */
                const char *gf = sfmt("tycho_arr_%s_grow", arr_fn(g_fuse[fi].ty));
                const char *ow = owner_arena_of(e->args[0]->sval);
                char *v = copy_into(arr_elem(g_fuse[fi].ty), ow, gen_expr(e->args[1], arena));
                return sfmt("({ if (_fl%d == _fc%d) %s(%s, &_fd%d, &_fc%d, _fl%d); _fd%d[_fl%d++] = %s; })",
                            id, id, gf, ow, id, id, id, id, id, v);
            }
        }
        Expr *root = e->args[0];
        while (root->kind == E_FIELD || root->kind == E_INDEX) root = root->lhs;
        /* regrow targets the array's owning arena — the carried _ina_ arena
         * if the root is a heap mut param (so the new buffer outlives the
         * call and the caller sees the updated descriptor). */
        const char *owner = (root->kind == E_IDENT) ? owner_arena_of(root->sval) : arena;
        char *arr = gen_lvalue(e->args[0], arena);   /* lvalue so a projected `arr[i].xs` is the buffer slot */
        char *v = gen_expr(e->args[1], arena);
        if (IS_SOA(e->args[0]->type))   /* struct-of-arrays push: grow each field buffer + scatter */
            return sfmt("Soa%d_push(%s, &(%s), %s)", SOA_ID(e->args[0]->type), owner, arr, v);
        /* push has the same (owner, &arr, v) shape for every element type */
        return sfmt("tycho_arr_%s_push(%s, &(%s), %s)", arr_fn(e->args[0]->type), owner, arr, v);
    }
    if (!strcmp(e->sval, "pop")) {
        /* remove + return the last element. The result is deep-copied into the
         * CURRENT arena (a heap element must outlive a later push that recycles
         * the buffer); scalars pass through. The array (root's arena) only shrinks. */
        char *arr = gen_lvalue(e->args[0], arena);
        if (IS_SOA(e->args[0]->type))   /* soa pop: shrink len, gather the last element (struct value) */
            return sfmt("Soa%d_pop(&(%s))", SOA_ID(e->args[0]->type), arr);
        return sfmt("tycho_arr_%s_pop(%s, &(%s))", arr_fn(e->args[0]->type), arena, arr);
    }
    if (!strcmp(e->sval, "reserve")) {           /* preallocate exact capacity (same shape as push) */
        Expr *root = e->args[0];
        while (root->kind == E_FIELD || root->kind == E_INDEX) root = root->lhs;
        const char *owner = (root->kind == E_IDENT) ? owner_arena_of(root->sval) : arena;
        char *arr = gen_lvalue(e->args[0], arena);
        char *n = gen_expr(e->args[1], arena);
        return sfmt("tycho_arr_%s_reserve(%s, &(%s), %s)", arr_fn(e->args[0]->type), owner, arr, n);
    }
    if (!strcmp(e->sval, "split")) {
        char *s   = gen_expr(e->args[0], arena);
        char *sep = gen_expr(e->args[1], arena);
        return sfmt("tycho_str_split(%s, %s, %s)", arena, s, sep);
    }
    if (!strcmp(e->sval, "print")) {
        if (is_extern_str_call(e->args[0]))   /* FFI: write a C-owned string without copying it (read-once borrow) */
            return sfmt("tycho_print(({ const char *_x = %s; _x ? _x : \"\"; }))", gen_extern_raw(e->args[0]));
        char *a = gen_expr(e->args[0], arena);
        return sfmt("tycho_print_s(%s)", a);   /* a Tycho string: print all bytes via its length header */
    }
    if (!strcmp(e->sval, "eprint")) return sfmt("tycho_eprint(%s)", gen_expr(e->args[0], arena));   /* stderr, no newline, no exit */
    if (!strcmp(e->sval, "println")) {   /* print + a trailing newline */
        if (is_extern_str_call(e->args[0]))
            return sfmt("(tycho_print(({ const char *_x = %s; _x ? _x : \"\"; })), tycho_print(\"\\n\"))", gen_extern_raw(e->args[0]));
        char *a = gen_expr(e->args[0], arena);
        return sfmt("(tycho_print_s(%s), tycho_print(\"\\n\"))", a);
    }
    if (!strcmp(e->sval, "input")) {
        return sfmt("tycho_input(%s)", arena);
    }
    if (!strcmp(e->sval, "read_all")) {
        return sfmt("tycho_read_all(%s)", arena);
    }
    if (!strcmp(e->sval, "clock")) return "tycho_clock()";   /* monotonic ns; no arena (scalar) */
    if (!strcmp(e->sval, "now"))   return "tycho_now()";     /* wall-clock seconds */
    if (!strcmp(e->sval, "ncpu"))  return "((long)tycho_ncpu())";   /* parallel-for fan-out width */
    if (!strcmp(e->sval, "read_file")) {
        return sfmt("tycho_read_file(%s, %s)", arena, gen_expr(e->args[0], arena));
    }
    if (!strcmp(e->sval, "getenv")) {   /* env var value, or "" if unset */
        return sfmt("tycho_getenv(%s, %s)", arena, gen_expr(e->args[0], arena));
    }
    if (!strcmp(e->sval, "is_null")) {   /* FFI: opaque-handle NULL test */
        return sfmt("((%s) == 0)", gen_expr(e->args[0], arena));
    }
    if (!strcmp(e->sval, "wait") && e->nargs == 1 && IS_TASK(e->args[0]->type)) {
        /* join, deep-copy the result out of the task's root arena into the
         * destination arena, then free the whole task tree (the blocks recycle
         * into THIS thread's pool -- the freeing thread owns them now). */
        Type rt = task_inner(e->args[0]->type);
        char *tv = gen_expr(e->args[0], arena);
        char *res = copy_into(rt, arena, sfmt("(*(%s*)_tk->ret)", c_type(rt)));
        if (e->args[0]->kind == E_SPAWN)   /* anonymous wait(spawn ...): unaliasable -- free everything now */
            return sfmt("({ HTask *_tk = %s; tycho_task_join(_tk); %s_w = %s; tycho_task_free(_tk); _w; })",
                        tv, c_type(rt), res);
        /* named task var (CC-2): reclaim the arena tree eagerly, but keep the
         * handle struct alive until the variable's scope-exit tycho_task_finish
         * -- that makes a second wait a defined runtime error (done flag), and
         * the finish's arena_free of the already-freed root is a no-op. */
        return sfmt("({ HTask *_tk = %s; tycho_task_join(_tk); %s_w = %s; arena_free(&_tk->root); _w; })",
                    tv, c_type(rt), res);
    }
    if (!strcmp(e->sval, "channel") && IS_CHAN(e->type))   /* CC-4 */
        return sfmt("tycho_chan_new(%s)", gen_expr(e->args[0], arena));
    if (!strcmp(e->sval, "send") && e->nargs == 2 && IS_CHAN(e->args[0]->type)) {
        /* the generated per-type wrapper deep-copies the value into the
         * claimed slot's arena under the channel mutex */
        return sfmt("tycho_chan_send_%d(%s, %s)", CHAN_ID(e->args[0]->type),
                    gen_expr(e->args[0], arena), gen_expr(e->args[1], g_cur_scope));
    }
    if (!strcmp(e->sval, "recv") && e->nargs == 1 && IS_CHAN(e->args[0]->type)) {
        /* Option(T) result: has=0 (None) means closed and drained; the value
         * is deep-copied straight into the destination arena */
        return sfmt("({ TychoOpt%d _co = {0}; _co.has = (char)tycho_chan_recv_%d(%s, %s, &_co.val); _co; })",
                    OPT_ID(e->type), CHAN_ID(e->args[0]->type),
                    gen_expr(e->args[0], arena), arena);
    }
    if (!strcmp(e->sval, "close") && e->nargs == 1 && IS_CHAN(e->args[0]->type))
        return sfmt("tycho_chan_close(%s)", gen_expr(e->args[0], arena));
    if (!strcmp(e->sval, "close") && e->nargs == 1 && IS_HANDLE(e->args[0]->type)) {
        /* early close: run the destructor now and NULL the handle so the
         * (null-guarded) scope-exit finalizer is a no-op -- the user's C
         * free_fn runs exactly once even though both paths reach it. */
        const char *ff = g_handles[HANDLE_ID(e->args[0]->type)].free_fn;
        char *hv = gen_expr(e->args[0], arena);   /* h_<name> */
        return sfmt("({ if (%s) { %s(%s); %s = 0; } })", hv, ff, hv, hv);
    }
    if (!strcmp(e->sval, "write_file")) {   /* (path, contents) -> bool; no arena (no alloc) */
        return sfmt("tycho_write_file(%s, %s)", gen_expr(e->args[0], arena), gen_expr(e->args[1], arena));
    }
    if (!strcmp(e->sval, "list_dir")) {
        return sfmt("tycho_list_dir(%s, %s)", arena, gen_expr(e->args[0], arena));
    }
    if (!strcmp(e->sval, "args")) {
        return sfmt("tycho_args(%s)", arena);
    }
    if (!strcmp(e->sval, "chr")) {
        return sfmt("tycho_chr(%s, %s)", arena, gen_expr(e->args[0], arena));
    }
    if (!strcmp(e->sval, "die")) {   /* print to stderr and exit(1); never returns */
        return sfmt("tycho_die(%s)", gen_expr(e->args[0], arena));
    }
    if (!strcmp(e->sval, "str")) {
        char *a = gen_expr(e->args[0], arena);
        Type b = base_of(e->args[0]->type);
        if (b == T_STRING) return a;                 /* str(string) is identity (interpolation) */
        if (b == T_BOOL)   return sfmt("tycho_bool_to_str(%s, %s)", arena, a);
        if (b == T_FLOAT || b == T_F32) return sfmt("tycho_float_to_str(%s, %s)", arena, a);   /* f32 promotes to double */
        if (b == T_U32 || b == T_U64)   return sfmt("tycho_uint_to_str(%s, %s)", arena, a);
        return sfmt("tycho_int_to_str(%s, %s)", arena, a);
    }
    if (!strcmp(e->sval, "to_float"))    /* int -> double */
        return sfmt("((double)%s)", gen_expr(e->args[0], arena));
    if (!strcmp(e->sval, "to_int"))      /* double -> long, truncates toward zero */
        return sfmt("((long)%s)", gen_expr(e->args[0], arena));
    if (!strcmp(e->sval, "to_ptr"))      /* int -> void* (FFI sentinel pointer; tycho never derefs it) */
        return sfmt("((void*)(long)%s)", gen_expr(e->args[0], arena));
    if (!strcmp(e->sval, "to_i32"))      /* FFI: sign-extend a 32-bit C int return (low 32 bits are valid; the cast recovers the sign) */
        return sfmt("((long)(int)%s)", gen_expr(e->args[0], arena));
    if (!strcmp(e->sval, "to_u32"))      /* -> u32 (truncate to low 32 bits) */
        return sfmt("((unsigned int)%s)", gen_expr(e->args[0], arena));
    if (!strcmp(e->sval, "to_u64"))      /* -> u64 */
        return sfmt("((unsigned long long)%s)", gen_expr(e->args[0], arena));
    if (!strcmp(e->sval, "to_f32"))      /* -> f32 (single precision) */
        return sfmt("((float)%s)", gen_expr(e->args[0], arena));
    if (!strcmp(e->sval, "to_str") || !strcmp(e->sval, "to_bool") || !strcmp(e->sval, "to_under") || !strcmp(e->sval, "to_bytes"))   /* zero-cost newtype unwrap / bytes<->string reinterpret (identical char* repr) */
        return gen_expr(e->args[0], arena);
    if (!strcmp(e->sval, "sqrt") || !strcmp(e->sval, "floor") || !strcmp(e->sval, "fabs"))   /* libm, 1 float arg */
        return sfmt("%s(%s)", e->sval, gen_expr(e->args[0], arena));
    if (!strcmp(e->sval, "pow"))   /* libm, 2 float args */
        return sfmt("pow(%s, %s)", gen_expr(e->args[0], arena), gen_expr(e->args[1], arena));
    /* user proc: first arg is the destination arena for its return. A heap
     * mut parameter takes TWO C args: the value's owning arena, then the
     * &pointer — so an allocating mutation in the callee lands where the
     * value lives. The owner is computed from the argument's root variable
     * (which, if it's itself a heap mut param here, yields its carried
     * _ina_ arena — threading the real owner across recursion). */
    Sig *cs = sig_find(e->sval);
    if (cs && cs->is_extern) {
        /* FFI: call the C symbol directly — no arena, no h_ prefix. Args are
         * built in the current scope (tycho str is already a char*, so a string
         * arg passes zero-cost). A string RETURN is C-owned, so copy it into the
         * destination arena (NULL -> "") so tycho never holds a foreign pointer. */
        if (base_of(cs->ret) == T_BYTES) {
            /* out-param shim: synthesize (unsigned char** out, long* outlen), call,
             * then copy the C-owned buffer into an arena `bytes` and free it.
             * The shim must malloc(*out); tycho_bytes_from_c frees it after copying. */
            char *decls = sfmt("%s", ""), *args = sfmt("%s", "");
            int emitted = 0, nb = 0;
            for (int i = 0; i < e->nargs; i++) {
                char *a = gen_expr(e->args[i], g_cur_scope);
                if (e->args[i]->type == T_BYTES) {
                    char *tv = sfmt("_xb%d", nb++);
                    decls = sfmt("%schar *%s = %s; ", decls, tv, a);
                    args = sfmt("%s%s(const unsigned char *)%s, tycho_str_len(%s)", args, emitted++ ? ", " : "", tv, tv);
                } else {
                    args = sfmt("%s%s%s", args, emitted++ ? ", " : "", a);
                }
            }
            int id = g_blk++;
            return sfmt("({ %sunsigned char *_o%d = 0; long _ol%d = 0; %s(%s%s&_o%d, &_ol%d); tycho_bytes_from_c(%s, _o%d, _ol%d); })",
                        decls, id, id, e->sval, args, emitted ? ", " : "", id, id, arena, id, id);
        }
        char *xc = gen_extern_raw(e);
        /* FFI R3a: `-> Option(string)` — NULL becomes None, a real pointer becomes
         * Some(arena-copied). (Plain `-> string` below still maps NULL to "".) */
        if (IS_OPT(cs->ret) && opt_inner(cs->ret) == T_STRING)
            return sfmt("({ const char *_x = %s; %s_o = {0}; if (_x) { _o.has = 1; _o.val = tycho_str_from_c(%s, _x); } _o; })", xc, c_type(cs->ret), arena);
        if (base_of(cs->ret) == T_STRING)
            return sfmt("tycho_str_from_c(%s, ({ const char *_x = %s; _x ? _x : \"\"; }))", arena, xc);
        return xc;
    }
    char *out = sfmt("h_%s(%s", e->sval, arena);
    for (int i = 0; i < e->nargs; i++) {
        /* arguments are transients (the callee's return value is independently
         * owned in _parent — never an alias of an arg), so build them in the
         * current scope, not the result arena which may be an outer scope. */
        /* PROTOTYPE sink: the callee OWNS this argument and may mutate it, so it must
         * be independent of the caller. arg_into adopts a movable dead local / fresh
         * value (no copy) and copies an otherwise-live place — exactly move-on-last-use
         * applied to the call boundary. (A borrowed param below just shares the buffer.) */
        int sink_arg = cs && i < cs->nparams && cs->sink[i] && type_is_heap(cs->params[i]);
        char *a = sink_arg ? sink_arg_into(cs->params[i], g_cur_scope, e->args[i])
                           : gen_expr(e->args[i], g_cur_scope);
        if (cs && i < cs->nparams && cs->mut[i] && type_is_heap(cs->params[i])
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
        case TK_PERCENT: return "%";
        case TK_AMP:   return "&";
        case TK_PIPE:  return "|";
        case TK_CARET: return "^";
        case TK_SHL:   return "<<";
        case TK_SHR:   return ">>";
        case TK_LT:    return "<";
        case TK_GT:    return ">";
        case TK_LE:    return "<=";
        case TK_GE:    return ">=";
        case TK_EQEQ:  return "==";
        case TK_NEQ:   return "!=";
        case TK_AND:   return "&&";
        case TK_OR:    return "||";
        default:       return "?";
    }
}

static char *gen_expr(Expr *e, const char *arena) {
    switch (e->kind) {
        case E_INT:
            /* u32/u64 literals need an unsigned C suffix: an unadorned `...L` is signed
             * long, so `u32_var + 3000000000L` would promote to signed 64-bit and wrap at
             * 2^64, not 2^32. `U`/`ULL` keep the arithmetic at the value's width. */
            if (e->type == T_U32) return sfmt("%ldU", e->ival);
            if (e->type == T_U64) return sfmt("%ldULL", e->ival);
            return sfmt("%ldL", e->ival);
        case E_CHAR: return sfmt("%ldL", e->ival);   /* a byte value carried as long */
        case E_FLOAT: {
            /* %.17g round-trips the double exactly; ensure the C literal reads
             * as a double (has '.', 'e', or is inf/nan) so e.g. 3.0 / 2.0 is
             * not integer division. */
            char b[64];
            snprintf(b, sizeof b, "%.17g", e->fval);
            int isfloaty = 0;
            for (char *q = b; *q; q++)
                if (*q == '.' || *q == 'e' || *q == 'E' || *q == 'n' || *q == 'i') { isfloaty = 1; break; }
            return isfloaty ? sfmt("%s", b) : sfmt("%s.0", b);
        }
        case E_BOOL: return sfmt("%ld", e->ival);
        case E_NULL: return sfmt("((void*)0)");
        case E_STR:  /* a length-headered, interned-once copy (cached per occurrence) */
            return sfmt("({ static char *_l = 0; if (!_l) _l = tycho_str_intern(\"%s\"); _l; })", e->sval);
        case E_NONE: return sfmt("(%s){0}", c_type(e->type));   /* has = 0 */
        case E_SOME: {
            Type inner = opt_inner(e->type);   /* move/copy the value into the option */
            return sfmt("(%s){ 1, %s }", c_type(e->type), arg_into(inner, arena, e->lhs));
        }
        case E_OK:    /* designated init: errv auto-zeroes, dodging -Wmissing-field-initializers */
            return sfmt("(%s){ .ok = 1, .okv = %s }", c_type(e->type),
                        arg_into(res_ok(e->type), arena, e->lhs));
        case E_ERR:
            return sfmt("(%s){ .ok = 0, .errv = %s }", c_type(e->type),
                        arg_into(res_err(e->type), arena, e->lhs));
        case E_ORRETURN: {
            /* ({ Tmp _or = <src>; if (!_or.ok) { promote err to _parent, free
             * the live arenas, return Err from the enclosing fn; } _or.okv; })
             * The err copy is taken BEFORE the frees (it points into a scope we
             * are about to free), exactly like the S_RETURN promotion. */
            int id = g_blk++;
            char *v = gen_expr(e->lhs, arena);
            char *rf = return_frees();
            if (IS_OPT(e->lhs->type)) {   /* unwrap Some(x) to x, else return None from the enclosing fn */
                return sfmt("({ %s_or%d = %s; if (!_or%d.has) { %s return (%s){0}; } _or%d.val; })",
                            c_type(e->lhs->type), id, v, id, rf, c_type(g_gen_ret), id);
            }
            char *promote = copy_into(res_err(g_gen_ret), "_parent", sfmt("_or%d.errv", id));
            return sfmt("({ %s_or%d = %s; if (!_or%d.ok) { %s_rr%d = (%s){ .ok = 0, .errv = %s }; %s return _rr%d; } _or%d.okv; })",
                        c_type(e->lhs->type), id, v, id,
                        c_type(g_gen_ret), id, c_type(g_gen_ret), promote, rf, id, id);
        }
        case E_TUPLE: {   /* (e1, ..., en): positional struct literal; heap places deep-copied in */
            char *out = sfmt("(%s){ ", c_type(e->type));
            for (int i = 0; i < e->nargs; i++) {
                char *a = arg_into(tup_elem(e->type, i), arena, e->args[i]);
                out = sfmt("%s%s%s", out, a, i + 1 < e->nargs ? ", " : "");
            }
            return sfmt("%s }", out);
        }
        case E_TUPIDX:   /* t.0 -> (t)._0 */
            return sfmt("((%s)._%ld)", gen_expr(e->lhs, arena), e->ival);
        case E_LAMBDA: {   /* closure creation: {env, thunk}; the env holds the captures, in the current scope arena */
            LamInfo *li = &g_laminfo[e->ival];
            int fid = FUNC_ID(li->ftype), id = (int)e->ival;
            if (li->ncap == 0)
                return sfmt("(FnC%d){0, __lam%d__clo, 0}", fid, id);
            /* env in the OWNER arena (where this closure value is stored) — so a
             * loop-local closure is reclaimed each iteration with the rest of its
             * block, not retained in the function arena for the whole call. Every
             * escape (return / store in an escaping container / assign to a longer-
             * lived var) re-homes the env via Env_<id>_copy (the 3rd field). */
            char *out = sfmt("({ Env_%d *_e = (Env_%d *)arena_alloc(%s, sizeof(Env_%d));", id, id, arena, id);
            for (int i = 0; i < li->ncap; i++) {
                const char *cn = li->proc->params[i].name;
                Type ct = li->proc->params[i].type;
                char *cv = is_inout_param(cn) ? sfmt("(*h_%s)", cn) : sfmt("h_%s", cn);
                if (type_is_heap(ct)) cv = copy_into(ct, arena, cv);   /* value semantics: own a deep copy in the env arena */
                out = sfmt("%s _e->c%d = %s;", out, i, cv);
            }
            return sfmt("%s (FnC%d){_e, __lam%d__clo, Env_%d_copy}; })", out, fid, id, id);
        }
        case E_IDENT:
            if (e->op == TK_FN)   /* a function reference -> the fat value {0, <name>__clo, 0} (no env to re-home) */
                return sfmt("(FnC%d){0, %s__clo, 0}", FUNC_ID(e->type), e->sval);
            return is_inout_param(e->sval) ? sfmt("(*h_%s)", e->sval)
                                           : sfmt("h_%s", e->sval);
        case E_ADDR: /* &place as a mut arg: address of the underlying
                      * lvalue. gen_lvalue derefs a mut root and projects an
                      * array element to its buffer slot, so `&arr[i].x` is a
                      * real address, not the address of a `_get` temporary. */
            return sfmt("&(%s)", gen_lvalue(e->lhs, arena));
        case E_SPAWN: {   /* copy args into the task's root arena, then start the thread */
            int id = (int)e->ival;
            Sig *s = &g_sigs[g_spawn[id]];
            Expr *c = e->lhs;
            char *out = sfmt("({ HTask *_tk = tycho_task_new(); "
                             "HSpawnA_%d *_sa = (HSpawnA_%d *)arena_alloc(&_tk->root, sizeof(HSpawnA_%d)); "
                             "_sa->t = _tk;", id, id, id);
            for (int i = 0; i < c->nargs; i++) {
                /* evaluate in the current scope, then deep-copy into the task
                 * root: after this the spawner and the task share zero bytes
                 * (a scalar's value word is already a complete copy). */
                char *a = gen_expr(c->args[i], g_cur_scope);
                if (type_is_heap(s->params[i])) a = copy_into(s->params[i], "(&_tk->root)", a);
                out = sfmt("%s _sa->a%d = %s;", out, i, a);
            }
            return sfmt("%s tycho_task_start(_tk, tycho_spawn_%d, _sa); _tk; })", out, id);
        }
        case E_CALL: return gen_call(e, arena);
        case E_INDEX: {
            if (IS_SOA(e->lhs->type)) {
                /* gather: assemble a struct value from the field buffers at i,
                 * bounds-checked once. The struct's fields are read shallowly;
                 * binding/passing/returning it deep-copies via is_place (so a
                 * heap field stays independent — value semantics). */
                int id = g_blk++;
                StructDef *sd = &g_structs[STRUCT_ID(soa_struct(e->lhs->type))];
                int sid = SOA_ID(e->lhs->type);
                char *sl = gen_lvalue(e->lhs, arena);
                char *ix = gen_expr(e->rhs, arena);
                char *out = sfmt("({ Soa%d *_g%d = &(%s); long _gi%d = Soa%d_bound(_g%d, %s); (S_%s){ ",
                                 sid, id, sl, id, sid, id, ix, sd->name);
                for (int f = 0; f < sd->nfields; f++)
                    out = sfmt("%s%s_g%d->f%d[_gi%d]", out, f ? ", " : "", id, f, id);
                return sfmt("%s }; })", out);
            }
            if (is_map(e->lhs->type)) {
                /* rvalue m[k] read for a COMPOSITE value type: a PURE map_get with the
                 * value's zero/empty (V){0} -- never inserts (the place path is
                 * gen_lvalue, not here). Scalar values were desugared to map_get at
                 * resolve, so only composites reach this. Deep-copy the borrow out, as
                 * map_get's codegen does, so it outlives a later mutation/free of m. */
                Type mt = e->lhs->type, vt = map_val(mt);
                char *m = gen_expr(e->lhs, arena);
                char *k = key_rt(mt, gen_expr(e->rhs, arena));
                char *call = sfmt("%s(%s, %s, (%s){0})", map_rt(mt, "get"), m, k, c_type(vt));
                return copy_into(vt, arena, call);
            }
            char *a = gen_expr(e->lhs, arena);
            char *ix = gen_expr(e->rhs, arena);
            if (e->lhs->type == T_STRING)
                return sfmt("tycho_str_get(%s, %s)", a, ix);   /* O(1): length header, no strlen */
            if (index_in_range(e->lhs, e->rhs))               /* monotone loop index: skip the bounds check */
                return sfmt("(%s).data[%s]", a, ix);
            return sfmt("tycho_arr_%s_get(%s, %s)", arr_fn(e->lhs->type), a, ix);
        }
        case E_SLICE: {
            if (e->lhs->type == T_STRING) {   /* s[a:b] -> a fresh substring (substr) */
                int id = g_blk++;
                char *s  = gen_expr(e->lhs, arena);
                char *lo = e->rhs ? gen_expr(e->rhs, arena) : sfmt("0L");
                char *hi = e->nargs ? gen_expr(e->args[0], arena) : sfmt("tycho_str_len(_ss%d)", id);
                return sfmt("({ const char *_ss%d = %s; tycho_str_substr(%s, _ss%d, %s, %s); })",
                            id, s, arena, id, lo, hi);
            }
            if (IS_SOA(e->lhs->type)) {
                /* soa sub-range: offset every field pointer by lo, len = hi-lo,
                 * cap = 0 (non-growable view aliasing the source buffers). Like
                 * the array slice, is_place(E_SLICE) deep-copies it on any store. */
                int id = g_blk++;
                int sid = SOA_ID(e->lhs->type);
                StructDef *sd = &g_structs[STRUCT_ID(soa_struct(e->lhs->type))];
                char *a  = gen_expr(e->lhs, arena);
                char *lo = e->rhs ? gen_expr(e->rhs, arena) : sfmt("0L");
                char *hi = e->nargs ? gen_expr(e->args[0], arena) : sfmt("_sv%d.len", id);
                char *fs = NULL;
                for (int f = 0; f < sd->nfields; f++)
                    fs = fs ? sfmt("%s, _sv%d.f%d + _lo%d", fs, id, f, id)
                            : sfmt("_sv%d.f%d + _lo%d", id, f, id);
                return sfmt("({ Soa%d _sv%d = %s; long _lo%d = %s, _hi%d = %s; "
                            "if (_lo%d < 0 || _hi%d > _sv%d.len || _lo%d > _hi%d) { "
                            "fprintf(stderr, \"tycho: slice [%%ld:%%ld] out of bounds (len %%ld)\\n\", _lo%d, _hi%d, _sv%d.len); exit(1); } "
                            "(Soa%d){ %s, _hi%d - _lo%d, 0 }; })",
                            sid, id, a, id, lo, id, hi,
                            id, id, id, id, id, id, id, id,
                            sid, fs, id, id);
            }
            /* A bounds-checked sub-range descriptor { data + lo, hi - lo, 0 }.
             * cap = 0 marks it non-growable; the data pointer ALIASES the source
             * buffer, so this is a zero-copy view — but is_place(E_SLICE) makes
             * any bind/return/push deep-copy it into an owning array. The source
             * is bound to a temp so it is evaluated exactly once (lo/hi may use
             * its .len). Works for every array type (all are {data,len,cap}). */
            int id = g_blk++;
            const char *ct = c_type(e->type);
            char *a  = gen_expr(e->lhs, arena);
            char *lo = e->rhs ? gen_expr(e->rhs, arena) : sfmt("0L");
            char *hi = e->nargs ? gen_expr(e->args[0], arena) : sfmt("_sv%d.len", id);
            return sfmt("({ %s_sv%d = %s; long _lo%d = %s, _hi%d = %s; "
                        "if (_lo%d < 0 || _hi%d > _sv%d.len || _lo%d > _hi%d) { "
                        "fprintf(stderr, \"tycho: slice [%%ld:%%ld] out of bounds (len %%ld)\\n\", _lo%d, _hi%d, _sv%d.len); exit(1); } "
                        "(%s){ _sv%d.data + _lo%d, _hi%d - _lo%d, 0 }; })",
                        ct, id, a, id, lo, id, hi,
                        id, id, id, id, id, id, id, id,
                        ct, id, id, id, id);
        }
        case E_ARRLIT: {
            /* GNU statement-expression so a literal is a single value */
            int id = g_blk++;
            if (IS_SOA(e->type))   /* empty soa literal `soa []Struct` (core supports empty only) */
                return sfmt("(Soa%d){0}", SOA_ID(e->type));
            if (is_map(e->type)) {
                /* map literal: build empty in `arena`, then put each pair. The
                 * runtime put copies the key bytes into `arena`. args interleave
                 * k0,v0,k1,v1,...; an empty literal (nargs 0) just yields {0}. */
                char *out = sfmt("({ %s_l%d = %s(%s, 0L);",
                                 c_type(e->type), id, map_rt(e->type, "with_cap"), arena);
                for (int i = 0; i + 1 < e->nargs; i += 2)
                    out = sfmt("%s %s(%s, &_l%d, %s, %s);",
                               out, map_rt(e->type, "put"), arena, id,
                               key_rt(e->type, gen_expr(e->args[i], arena)),
                               gen_expr(e->args[i + 1], arena));
                return sfmt("%s _l%d; })", out, id);
            }
            /* array literal: build with_cap, then store each element. copy_into
             * deep-copies it into `arena` so the array owns its bytes — a plain
             * assign for int/float, tycho_str_copy for string, the element's deep
             * copy for a struct or nested array. */
            Type elem = arr_elem(e->type);
            char *out = sfmt("({ %s_l%d = tycho_arr_%s_with_cap(%s, %dL);",
                             c_type(e->type), id, arr_fn(e->type), arena, e->nargs);
            for (int i = 0; i < e->nargs; i++)
                out = sfmt("%s _l%d.data[%d] = %s;", out, id, i,
                           arg_into(elem, arena, e->args[i]));
            return sfmt("%s _l%d.len = %dL; _l%d; })", out, id, e->nargs, id);
        }
        case E_FIELD: {
            if (e->lhs->kind == E_INDEX && IS_SOA(e->lhs->lhs->type)) {
                /* soa element field: index the contiguous field buffer, bounds-
                 * checked via Soa<id>_bound. This is a plain array subscript, so
                 * it is also a valid lvalue for `a[i].field = v` (S_FIELDSET). */
                Type st = soa_struct(e->lhs->lhs->type);
                StructDef *sd = &g_structs[STRUCT_ID(st)];
                int fi = 0; while (fi < sd->nfields && strcmp(sd->fields[fi].name, e->sval)) fi++;
                char *sl = gen_lvalue(e->lhs->lhs, arena);   /* the soa place (a variable/field) */
                char *ix = gen_expr(e->lhs->rhs, arena);
                return sfmt("((%s).f%d[Soa%d_bound(&(%s), %s)])",
                            sl, fi, SOA_ID(e->lhs->lhs->type), sl, ix);
            }
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
                char *a = arg_into(sd->fields[i].type, arena, e->args[i]);
                out = sfmt("%s%s%s", out, a, i + 1 < e->nargs ? ", " : "");
            }
            return sfmt("%s })", out);
        }
        case E_BINOP: {
            if (e->op == TK_IN)                /* `k in m` membership -> map has-key */
                return sfmt("%s(%s, %s)", map_rt(e->rhs->type, "has"),
                            gen_expr(e->rhs, arena), key_rt(e->rhs->type, gen_expr(e->lhs, arena)));
            if (e->op == TK_NOT)               /* unary: operand in lhs, rhs NULL */
                return sfmt("(!%s)", gen_expr(e->lhs, arena));
            if (e->op == TK_MINUS && e->rhs == NULL)   /* unary negation */
                return sfmt("(-%s)", gen_expr(e->lhs, arena));
            if (e->op == TK_TILDE)                     /* unary bitwise NOT */
                return sfmt("(~%s)", gen_expr(e->lhs, arena));
            /* multi-piece string concat: flatten an all-string left-spine chain
             * of 3..6 pieces to one tycho_str_concatN (one alloc + one copy per
             * piece) instead of N-2 chained tycho_str_concat intermediates. A char
             * piece, or a chain outside 3..6, falls through to the pairwise emit
             * below (which still flattens its own sub-chains). Argument evaluation
             * order is unspecified -- exactly as for the nested pairwise concat it
             * replaces -- so side-effect ordering is unchanged. */
            if (e->op == TK_PLUS && e->type == T_STRING) {
                Expr *pieces[6]; int np = 0, ok = 1;
                for (Expr *cur = e; ; ) {
                    int more = (cur->kind == E_BINOP && cur->op == TK_PLUS && cur->type == T_STRING);
                    Expr *leaf = more ? cur->rhs : cur;
                    if (leaf->type != T_STRING || np >= 6) { ok = 0; break; }
                    pieces[np++] = leaf;
                    if (!more) break;
                    cur = cur->lhs;
                }
                if (ok && np >= 3) {
                    char *out = sfmt("tycho_str_concat%d(%s", np, arena);
                    for (int k = np - 1; k >= 0; k--)   /* pieces are rightmost-first; emit leftmost first */
                        out = sfmt("%s, %s", out, gen_expr(pieces[k], arena));
                    return sfmt("%s)", out);
                }
            }
            char *l = gen_expr(e->lhs, arena);
            char *r = gen_expr(e->rhs, arena);
            /* and/or lower to C's short-circuiting && / || via op_str below */
            if (e->op == TK_PLUS && e->lhs->type == T_STRING)
                return e->rhs->type == T_CHAR
                    ? sfmt("tycho_str_concat_char(%s, %s, %s)", arena, l, r)
                    : sfmt("tycho_str_concat(%s, %s, %s)", arena, l, r);
            /* equality dispatches by type (deep/structural); != negates it */
            if (e->op == TK_EQEQ || e->op == TK_NEQ) {
                char *eq = gen_eq(e->lhs->type, l, r);
                return e->op == TK_EQEQ ? eq : sfmt("(!%s)", eq);
            }
            /* ordering on strings is lexicographic via strcmp */
            if (is_cmp(e->op) && base_of(e->lhs->type) == T_STRING)   /* string or a string newtype */
                return sfmt("(tycho_str_cmp(%s, %s) %s 0)", l, r, op_str(e->op));
            /* integer division/modulo guard: -fwrapv (the int-overflow contract)
             * makes signed overflow wrap, but division is the one arithmetic op it
             * does NOT make total -- x/0, x%0, and LONG_MIN/-1 still trap (SIGFPE).
             * Route int `/` and `%` (incl. an int newtype) through the runtime guard,
             * which aborts cleanly with a tycho: message. Float `/` is IEEE
             * (x/0.0 = inf), so it stays a raw C operator. */
            if ((e->op == TK_SLASH || e->op == TK_PERCENT) && base_of(e->type) == T_INT)
                return sfmt("%s(%s, %s)", e->op == TK_SLASH ? "tycho_idiv" : "tycho_imod", l, r);
            if ((e->op == TK_SLASH || e->op == TK_PERCENT) && (base_of(e->type) == T_U32 || base_of(e->type) == T_U64))
                return sfmt("%s(%s, %s)", e->op == TK_SLASH ? "tycho_udiv" : "tycho_umod", l, r);
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
    if (n <= 0) return 0;
    Stmt *s = body[n - 1];
    switch (s->kind) {
        case S_RETURN:
            return 1;
        case S_MATCH:
            /* match is exhaustive (enforced in checking); if every arm ends in
             * a return, the whole match returns on every path. */
            for (int i = 0; i < s->narms; i++)
                if (!block_ends_in_return(s->arms[i].body, s->arms[i].nbody))
                    return 0;
            return s->narms > 0;
        case S_IF:
            /* only total when an else exists and both sides return (an elif
             * chain is an S_IF nested in els, handled by the recursion). */
            return s->nels > 0
                && block_ends_in_return(s->body, s->nbody)
                && block_ends_in_return(s->els, s->nels);
        default:
            return 0;
    }
}

/* Stack of enclosing loop/if block arenas live at the current codegen point,
 * outermost-first (e.g. "&_scr3", "&_b7"). Each is an INDEPENDENT arena
 * (arena_child is a fresh block list, not physically nested in _scope), so an
 * early `return` must free them explicitly or they leak — this is the
 * loop-return scratch leak. Reset per proc; pushed around each block body. */
static const char *g_ascope[64];
static int g_nascope = 0;
static void ascope_push(const char *a) {
    if (g_nascope >= 64) { fprintf(stderr, "tychoc: block nesting too deep\n"); exit(1); }
    g_ascope[g_nascope++] = a;
}

/* Live task/channel finalizer calls (CC-2 implicit join, CC-4 channel free).
 * Every scope exit emits the stored calls for the handles dying there --
 * gen_block covers normal block ends (incl. each loop iteration and the proc
 * body), return_frees covers early return + or_return, and break/continue
 * cover loop escapes via the loop-entry mark. Emission is LIFO, so a channel
 * (declared before the tasks that use it) is freed AFTER those tasks join.
 * Reset per proc. */
static const char **g_taskvars;   /* full finalizer calls, e.g. "tycho_task_finish(h_t)" */
static int g_taskvars_cap = 0;
static int g_ntaskvars = 0;
static int g_loop_taskmark[64];   /* g_ntaskvars at each enclosing loop's body entry */
static void taskvar_push(const char *call) {
    TBL_ENSURE(g_taskvars, g_ntaskvars, g_taskvars_cap);
    g_taskvars[g_ntaskvars++] = call;
}
static char *task_finishes_from(int mark) {   /* "" when none are dying */
    char *s = sfmt("%s", "");
    for (int i = g_ntaskvars - 1; i >= mark; i--)
        s = sfmt("%s%s; ", s, g_taskvars[i]);
    return s;
}

/* The free sequence an (early) return must run: every enclosing block arena
 * innermost-first, then the proc's own _scope. The return value already lives
 * in _parent (built or deep-copied there), which strictly outlives all of
 * these, so freeing them here can never touch the returned bytes. At proc top
 * level (g_nascope == 0) this is exactly "arena_free(&_scope);" — unchanged,
 * so a top-level return emits byte-identical C to before. */
static char *return_frees(void) {
    char *s = task_finishes_from(0);   /* implicit join: a return first joins every live task (CC-2) */
    for (int i = g_nascope - 1; i >= 0; i--)
        s = sfmt("%sarena_free(%s); ", s, g_ascope[i]);
    return sfmt("%sarena_free(&_scope);", s);
}

/* `scope` is a C expression of type Arena* into which local allocations
 * go. Returns always promote/collapse the proc's own arena, named
 * "_scope" in every generated proc body. */
/* A C lvalue for a place expression — the mutable-mutation counterpart of
 * gen_expr. For E_IDENT/E_FIELD it is exactly what gen_expr already produces
 * (a variable, or `(place).f_x`). The difference is E_INDEX: instead of the
 * by-value `_get` copy, a composite-array element yields a pointer into the
 * backing buffer via the bounds-checked tycho_arr_C<id>_ptr, dereferenced to a
 * real lvalue. So `arr[i].f = v`, `m[i][j] = v`, `push(arr[i].xs, v)`, and
 * `&arr[i].x` (mut) all mutate the element in place — Hylo-style projection,
 * with no pointer ever surfaced in Tycho. Only ARRC bases are projected
 * (is_lvalue guarantees it); other kinds fall back to gen_expr. */
static char *gen_lvalue(Expr *e, const char *arena) {
    if (e->kind == E_FIELD) {
        if (e->lhs->kind == E_INDEX && IS_SOA(e->lhs->lhs->type)) {   /* soa[i].field place */
            Type st = soa_struct(e->lhs->lhs->type);
            StructDef *sd = &g_structs[STRUCT_ID(st)];
            int fi = 0; while (fi < sd->nfields && strcmp(sd->fields[fi].name, e->sval)) fi++;
            char *sl = gen_lvalue(e->lhs->lhs, arena);
            char *ix = gen_expr(e->lhs->rhs, arena);
            return sfmt("((%s).f%d[Soa%d_bound(&(%s), %s)])",
                        sl, fi, SOA_ID(e->lhs->lhs->type), sl, ix);
        }
        return sfmt("((%s).f_%s)", gen_lvalue(e->lhs, arena), e->sval);
    }
    if (e->kind == E_TUPIDX)
        return sfmt("((%s)._%ld)", gen_lvalue(e->lhs, arena), e->ival);
    if (e->kind == E_INDEX) {
        if (is_map(e->lhs->type)) {   /* m[k] place: find-or-insert, deref the value slot (#2) */
            Expr *root = e->lhs;
            while (root->kind == E_FIELD || root->kind == E_INDEX || root->kind == E_TUPIDX) root = root->lhs;
            /* slotptr allocates (rehash + key copy + value zero) into the MAP's
             * owning arena, so the inserted value lives as long as the map. */
            const char *owner = (root->kind == E_IDENT) ? owner_arena_of(root->sval) : arena;
            return sfmt("(*%s(%s, &(%s), %s))",
                        map_rt(e->lhs->type, "slotptr"), owner,
                        gen_lvalue(e->lhs, arena), key_rt(e->lhs->type, gen_expr(e->rhs, arena)));
        }
        if (index_in_range(e->lhs, e->rhs))   /* monotone loop index: project without the bounds check */
            return sfmt("((%s).data[%s])", gen_lvalue(e->lhs, arena), gen_expr(e->rhs, arena));
        return sfmt("(*tycho_arr_C%d_ptr(&(%s), %s))",
                    ARRC_ID(e->lhs->type), gen_lvalue(e->lhs, arena),
                    gen_expr(e->rhs, arena));
    }
    return gen_expr(e, arena);
}

/* CC-3 parallel for site: spawn K = tycho_ncpu() chunk tasks of the lifted
 * __par<N> proc through the CC-1 trampoline (HSpawnA_<sid>/tycho_spawn_<sid>),
 * deep-copying every capture into each task's root arena, then join in chunk
 * order and fold the partials into the real accumulators. All tasks join
 * inside this statement -- structured, nothing for CC-2 to track. */
static void gen_parfor(FILE *o, Stmt *s, int ind, const char *scope) {
    ParFor *pf = &g_parfor[s->par_id];
    int sid = pf->spawn_id;
    char *lo = gen_expr(s->r_start, scope), *hi = gen_expr(s->r_stop, scope);
    indent(o, ind); fprintf(o, "{\n");
    indent(o, ind + 1); fprintf(o, "long _plo = %s, _phi = %s;\n", lo, hi);
    indent(o, ind + 1); fprintf(o, "if (_phi < _plo) _phi = _plo;\n");
    indent(o, ind + 1); fprintf(o, "long _pk = tycho_ncpu();\n");
    indent(o, ind + 1); fprintf(o, "if (_pk > _phi - _plo) _pk = _phi - _plo;\n");
    indent(o, ind + 1); fprintf(o, "if (_pk < 1) _pk = 1; if (_pk > 64) _pk = 64;\n");
    indent(o, ind + 1); fprintf(o, "HTask *_pts[64];\n");
    indent(o, ind + 1); fprintf(o, "for (long _pc = 0; _pc < _pk; _pc++) {\n");
    indent(o, ind + 2); fprintf(o, "HTask *_tk = tycho_task_new();\n");
    indent(o, ind + 2); fprintf(o, "HSpawnA_%d *_sa = (HSpawnA_%d *)arena_alloc(&_tk->root, sizeof(HSpawnA_%d));\n", sid, sid, sid);
    indent(o, ind + 2); fprintf(o, "_sa->t = _tk;\n");
    indent(o, ind + 2); fprintf(o, "_sa->a0 = _plo + (_phi - _plo) * _pc / _pk;\n");
    indent(o, ind + 2); fprintf(o, "_sa->a1 = _plo + (_phi - _plo) * (_pc + 1) / _pk;\n");
    for (int i = 0; i < pf->ncap; i++) {
        Type ct = g_sigs[pf->sig].params[2 + i];
        char *cv = gen_expr(pf->caps[i], scope);
        if (type_is_heap(ct)) cv = copy_into(ct, "(&_tk->root)", cv);
        indent(o, ind + 2); fprintf(o, "_sa->a%d = %s;\n", 2 + i, cv);
    }
    indent(o, ind + 2); fprintf(o, "tycho_task_start(_tk, tycho_spawn_%d, _sa);\n", sid);
    indent(o, ind + 2); fprintf(o, "_pts[_pc] = _tk;\n");
    indent(o, ind + 1); fprintf(o, "}\n");
    indent(o, ind + 1); fprintf(o, "for (long _pc = 0; _pc < _pk; _pc++) {\n");
    indent(o, ind + 2); fprintf(o, "tycho_task_join(_pts[_pc]);\n");
    indent(o, ind + 2); fprintf(o, "%s_pp = *(%s*)_pts[_pc]->ret;\n", c_type(g_sigs[pf->sig].ret), c_type(g_sigs[pf->sig].ret));
    indent(o, ind + 2); fprintf(o, "tycho_task_free(_pts[_pc]);\n");
    for (int i = 0; i < pf->nacc; i++) {
        char *tgt = is_inout_param(pf->accs[i]) ? sfmt("(*h_%s)", pf->accs[i]) : sfmt("h_%s", pf->accs[i]);
        char *part = pf->nacc == 1 ? sfmt("_pp") : sfmt("_pp._%d", i);
        indent(o, ind + 2);
        fprintf(o, "%s = %s %s %s;\n", tgt, tgt, pf->accop[i] == TK_STAR ? "*" : "+", part);
    }
    indent(o, ind + 1); fprintf(o, "}\n");
    indent(o, ind); fprintf(o, "}\n");
}

static void gen_stmt(FILE *o, Stmt *s, int ind, const char *scope, Type ret) {
    /* call arguments in this statement's expressions are transients owned by
     * the current scope (see g_cur_scope). Set before generating any expr;
     * the current statement's expressions are always emitted before recursing
     * into nested blocks, so a nested gen_stmt re-setting this is harmless. */
    g_cur_scope = scope;
    if (g_line_info && s->line > 0)   /* -g: map this statement's C back to its .ty source line */
        fprintf(o, "#line %d \"%s\"\n", s->line, g_line_file);
    switch (s->kind) {
        case S_DECL: {
            if (s->ctrl) {   /* `x := if.../match...` (ROADMAP 2.1): declare, then let the
                              * rewritten `name = tail` branches fill it. An array is zero-init'd
                              * so the reassign-recycle guard (which reads .data) sees NULL on the
                              * still-empty slot; every path assigns before any read. */
                indent(o, ind);
                fprintf(o, "%sh_%s%s;\n", c_type(s->decl_type), s->name,
                        is_array(s->decl_type) ? " = {0}" : "");
                cv_push(s->name, scope);
                gen_stmt(o, s->ctrl, ind, scope, ret);
                break;
            }
            if (IS_TASK(s->decl_type))   /* CC-2: track for implicit join at this var's scope exit */
                taskvar_push(sfmt("tycho_task_finish(h_%s)", s->name));
            if (IS_CHAN(s->decl_type) && s->expr->kind == E_CALL
                && s->expr->sval && !strcmp(s->expr->sval, "channel"))
                taskvar_push(sfmt("tycho_chan_free(h_%s)", s->name));   /* CC-4: creator scope owns + frees */
            if (IS_HANDLE(s->decl_type))   /* FFI R2: RAII — emit the destructor free_fn(h) at this var's scope exit, null-guarded so an early close(h) isn't double-freed */
                taskvar_push(sfmt("if (h_%s) %s(h_%s)", s->name, g_handles[HANDLE_ID(s->decl_type)].free_fn, s->name));
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
            /* MM-10b: a top-level SCALAR decl whose RHS allocates (has a call) — the
             * RHS's heap is all transient (the result is a scalar copied out by value),
             * and at function top level there is no per-iteration reset to reclaim it,
             * so it would sit in _scope until function return. Build the RHS in a
             * per-statement _t arena freed immediately. Gated to "&_scope": inside a
             * loop/block the scratch reset already reclaims, so no hot-loop overhead.
             * (tychoc0's scalar_transient wraps at every depth; this tychoc-only gating
             * is output-invisible — reclaim only — so the fixpoint stays green.) */
            if (!type_is_heap(s->decl_type) && !strcmp(scope, "&_scope")
                && expr_has_call(s->expr) && !expr_has_orreturn(s->expr)) {
                g_cur_scope = "&_t";
                char *tv = gen_expr(s->expr, "&_t");
                indent(o, ind);
                /* self-referential shadow (`y := dbl(y)`): the new local must not
                 * be in scope while the transient RHS reads it. Land the result in
                 * a temp first, then bind -- so the RHS reads the enclosing binding
                 * (see the normal branch below for the full rationale). */
                if (expr_refs_local(s->expr, s->name))
                    fprintf(o, "%s_sh_%s; { Arena _t = arena_new(0); _sh_%s = %s; arena_free(&_t); } %sh_%s = _sh_%s;\n",
                            c_type(s->decl_type), s->name, s->name, tv,
                            c_type(s->decl_type), s->name, s->name);
                else
                    fprintf(o, "%sh_%s; { Arena _t = arena_new(0); h_%s = %s; arena_free(&_t); }\n",
                            c_type(s->decl_type), s->name, s->name, tv);
                cv_push(s->name, scope);
                break;
            }
            char *v = gen_expr(s->expr, owner);
            /* value semantics: binding from a heap *place* aliases its bytes,
             * so deep-copy into the owner arena. A literal/call/concat result
             * is already a freshly-owned value built in `owner` — no copy. And a
             * dead same-arena local is MOVED (copy elided): it takes over the
             * source's buffer (see can_move_from). */
            if (is_place(s->expr) && type_is_heap(s->decl_type) && !can_move_from(s->expr, owner))
                v = copy_into(s->decl_type, owner, v);
            indent(o, ind);
            /* Go/Odin lexical scope + value semantics: a self-referential shadow
             * `y := y + 2` reads the ENCLOSING y (the typechecker already bound the
             * RHS to it -- the decl's type is computed before the name is in scope),
             * then binds a fresh independent value. Evaluate the RHS into a temp
             * BEFORE the new C local is in scope, so the initializer reads the outer
             * binding -- not the uninitialized inner one (that was a use-before-init
             * UB: garbage on macOS, an accidental 0 on Linux). The temp name can't
             * collide: a second decl of `name` in one scope is rejected. */
            if (expr_refs_local(s->expr, s->name))
                fprintf(o, "%s_sh_%s = %s; %sh_%s = _sh_%s;\n",
                        c_type(s->decl_type), s->name, v,
                        c_type(s->decl_type), s->name, s->name);
            else
                fprintf(o, "%sh_%s = %s;\n", c_type(s->decl_type), s->name, v);
            /* in-place append sidecars, declared HERE (same C scope as h_v, so
             * a loop-body accumulator re-inits them each iteration in lockstep
             * with its buffer — never hoist these). cap 0 = "not growable in
             * place yet", so the first append allocates and the initial buffer
             * (possibly a string literal in .rodata) is never written. */
            if (s->decl_type == T_STRING && is_accum(s->name)) {
                indent(o, ind);
                fprintf(o, "long _len_h_%s = ((const long *)h_%s)[-1]; long _cap_h_%s = 0;\n",
                        s->name, s->name, s->name);
            }
            cv_push(s->name, owner);   /* this variable lives in `owner` */
            break;
        }
        case S_MDECL: {
            /* a, b := f() — build the tuple in `scope` (the call promotes its
             * returns there), then bind each name to an element. Each element
             * is an independently-owned value inside the tuple, so the binds
             * alias directly with no extra copy; they live in `scope`. */
            int id = g_blk++;
            Type tt = s->expr->type;
            char *v = gen_expr(s->expr, scope);
            indent(o, ind);
            fprintf(o, "%s_mt%d = %s;\n", c_type(tt), id, v);
            for (int i = 0; i < s->nnames; i++) {
                indent(o, ind);
                fprintf(o, "%sh_%s = _mt%d._%d;\n", c_type(s->mtypes[i]), s->names[i], id, i);
                cv_push(s->names[i], scope);
            }
            break;
        }
        case S_MASSIGN: {   /* a, b = f() — build the tuple, assign each element to its existing var */
            int id = g_blk++;
            Type tt = s->expr->type;
            char *v = gen_expr(s->expr, scope);
            indent(o, ind);
            fprintf(o, "%s_mt%d = %s;\n", c_type(tt), id, v);
            for (int i = 0; i < s->nnames; i++) {
                const char *owner = cv_arena(s->names[i]);
                if (!owner) owner = scope;
                char *ev = sfmt("_mt%d._%d", id, i);
                if (type_is_heap(s->mtypes[i])) ev = copy_into(s->mtypes[i], owner, ev);
                indent(o, ind);
                if (is_inout_param(s->names[i]))
                    fprintf(o, "(*h_%s) = %s;\n", s->names[i], ev);
                else
                    fprintf(o, "h_%s = %s;\n", s->names[i], ev);
            }
            break;
        }
        case S_ASSIGN: {
            /* allocate the value where the variable lives, not where we
             * currently are, so it survives any inner scope collapsing. For a
             * heap mut param the value lives in the caller's arena (_ina_),
             * so a whole-map/array reassignment must build there, not in this
             * callee's _scope (which would dangle once the call returns). */
            const char *owner = cv_arena(s->name);
            if (!owner) owner = scope;
            if (is_heap_inout_param(s->name)) owner = owner_arena_of(s->name);
            /* MM-10b: a top-level SCALAR assign with an allocating RHS — the RHS heap
             * is all transient (result is a scalar), and at function top level there
             * is no per-iteration reset to reclaim it, so it would sit in _scope until
             * return. Build it in a per-statement _t arena. Gated to "&_scope" (in a
             * loop/block the scratch reset already reclaims) and excludes or_return
             * (early return would skip arena_free) and mut (LHS is *h_, leave it). */
            if (!strcmp(scope, "&_scope") && !type_is_heap(s->expr->type)
                && expr_has_call(s->expr) && !expr_has_orreturn(s->expr)
                && !is_inout_param(s->name)) {
                g_cur_scope = "&_t";
                char *tv = gen_expr(s->expr, "&_t");
                indent(o, ind);
                fprintf(o, "{ Arena _t = arena_new(0); h_%s = %s; arena_free(&_t); }\n", s->name, tv);
                break;
            }
            /* in-place append: `acc = acc + e` on a tracked accumulator grows
             * acc's buffer in its OWNER arena (cv_arena), not the current loop
             * scratch scope. The append result re-homes acc, so the rest of
             * the function still sees an ordinary NUL-terminated char*. e is
             * fully evaluated before the buffer is touched (handles acc=acc+acc
             * and acc=acc+f(acc)). */
            /* (a mut param is excluded: its _len_/_cap_ trackers are never
             * declared and the target is (*h_x) — the generic path below
             * concats into _ina_ instead, which is correct, just not in-place) */
            if (is_accum(s->name) && is_self_append(s) && !is_inout_param(s->name)) {
                Expr *ops[64];
                int nops = collect_append_ops(s->expr, s->name, ops, 64);
                /* Multi-piece (k>=2) in-place is sound only if no operand reads
                 * the accumulator itself: the first append mutates acc's buffer,
                 * so an operand aliasing it (acc, substr(acc,..), acc[i:]) would
                 * then see the GROWN value. By value semantics acc uniquely owns
                 * its buffer, so the only way to alias it is to name it -- which
                 * count_reads_e finds. The single-piece (k=1) path keeps the old
                 * behavior: one operand passed straight to the append, so the
                 * runtime still handles acc=acc+acc / acc=acc+f(acc). */
                int alias_safe = 1;
                if (nops >= 2)
                    for (int k = 0; k < nops; k++)
                        if (count_reads_e(ops[k], s->name)) { alias_safe = 0; break; }
                if (nops == 1 || (nops >= 2 && alias_safe)) {
                    /* a char piece appends one byte in place (no strlen); a string
                     * piece appends its bytes. Both grow the same buffer. */
                    if (nops >= 2) {
                        /* Pre-evaluate ALL operands against acc's ORIGINAL value
                         * into temps BEFORE any append, then append in source
                         * order -- matches the "evaluate the whole RHS first"
                         * semantics of the original full concat. */
                        int id0 = g_blk; g_blk += nops;
                        for (int k = 0; k < nops; k++) {
                            char *e = gen_expr(ops[k], owner);
                            indent(o, ind);
                            fprintf(o, "%s _ap%d = %s;\n", ops[k]->type == T_CHAR ? "char" : "char*", id0 + k, e);
                        }
                        for (int k = 0; k < nops; k++) {
                            indent(o, ind);
                            fprintf(o, "%s(%s, &h_%s, &_len_h_%s, &_cap_h_%s, _ap%d);\n",
                                    ops[k]->type == T_CHAR ? "tycho_str_append_char" : "tycho_str_append",
                                    owner, s->name, s->name, s->name, id0 + k);
                        }
                    } else {
                        char *e = gen_expr(ops[0], owner);
                        indent(o, ind);
                        fprintf(o, "%s(%s, &h_%s, &_len_h_%s, &_cap_h_%s, %s);\n",
                                ops[0]->type == T_CHAR ? "tycho_str_append_char" : "tycho_str_append",
                                owner, s->name, s->name, s->name, e);
                    }
                    break;
                }
                /* else (an operand aliases acc, or > 64 pieces): fall through to
                 * the general assign -- a fresh full concat. Correct, not in-place. */
            }
            /* in-place map accumulator: `m = map_set(m, k, v)` grows m's unique
             * table in its OWNER arena via put, instead of the pure deep-copy
             * set. The key/value args are fully evaluated before the put runs
             * (so `map_set(m, w, map_get(m, w, 0) + 1)` reads the old m first);
             * no sidecars needed — len/cap live inside TychoMapSI. */
            if (is_accum(s->name) && is_self_mapset(s)) {
                /* the map's owning arena and a pointer to its descriptor: for an
                 * mut map param the descriptor is the caller's (pointer h_m,
                 * arena _ina_m) so the put lands where the value lives; for a
                 * local it is &h_m in the local's own arena. */
                const char *mo = owner_arena_of(s->name);
                const char *mp = is_heap_inout_param(s->name) ? sfmt("h_%s", s->name)
                                                              : sfmt("&h_%s", s->name);
                char *k = key_rt(s->expr->type, gen_expr(s->expr->args[1], mo));
                char *v = gen_expr(s->expr->args[2], mo);
                indent(o, ind);
                fprintf(o, "%s(%s, %s, %s, %s);\n", map_rt(s->expr->type, "put"), mo, mp, k, v);
                break;
            }
            /* in-place map delete: `m = map_del(m, k)` backward-shifts in place. No
             * allocation, so no arena arg; the pointer is the mut pointer for
             * a mut map, else the address of the local descriptor. */
            if (is_accum(s->name) && is_self_mapdel(s)) {
                const char *mo = owner_arena_of(s->name);
                const char *mp = is_heap_inout_param(s->name) ? sfmt("h_%s", s->name)
                                                              : sfmt("&h_%s", s->name);
                char *k = key_rt(s->expr->type, gen_expr(s->expr->args[1], mo));
                indent(o, ind);
                fprintf(o, "%s(%s, %s);\n", map_rt(s->expr->type, "del"), mp, k);
                break;
            }
            /* a self-rebuild `t = C(..., t, ...)` hands off t's buffer into
             * the new aggregate instead of copying it, even inside a loop. */
            int handed_off = self_rebuild_move(s);   /* the RHS takes over s's old buffer */
            if (handed_off) g_self_move_name = s->name;
            /* If the result is non-heap (a scalar), nothing heap escapes this
             * statement, so any heap TRANSIENT in the RHS — e.g. a tree built
             * only to be folded to an int in `sum = sum + check(make(d))` —
             * should be reclaimed with the CURRENT scope, not retained in the
             * target's (possibly outer-loop) arena. Build it in `scope`; the
             * scalar lands in the C variable and needs no arena. */
            const char *rhs_arena = type_is_heap(s->expr->type) ? owner : scope;
            char *v = gen_expr(s->expr, rhs_arena);
            g_self_move_name = NULL;
            /* a heap *place* is only an alias into some (possibly inner,
             * soon-to-collapse) scope; deep-copy it into the target's arena
             * so it survives. A literal/call/concat result is already freshly
             * allocated in `owner` — no copy needed. */
            if (is_place(s->expr) && type_is_heap(s->expr->type) && !can_move_from(s->expr, owner))
                v = copy_into(s->expr->type, owner, v);
            /* liveness-driven recycle: a loop-carried scalar-array local being
             * reassigned from a FUNCTION CALL (`a = step(a)`). A Tycho call returns
             * a freshly-owned value that -- by value semantics -- never aliases an
             * argument, so a's OLD buffer is dead and uniquely owned the instant
             * the call returns (the arg was only borrowed). Hand it back to its
             * arena so the NEXT iteration's allocation reuses it, instead of the
             * arena growing unbounded. FBIP reuse from STATIC value semantics --
             * no refcount. Restricted to a plain call (op != TK_ENUM excludes enum
             * constructors, which INCORPORATE the arg) and scalar-element arrays
             * (one buffer, no nested heap). v is built above; the assign is below.
             * (void)handed_off: the move-elision of the arg copy is orthogonal --
             * step still only reads the borrowed buffer, so it's dead afterward. */
            (void)handed_off;
            /* SOUNDNESS: `name` must be read >= 2 times in the function. move-on-
             * last-use (can_move_from) only moves a var read EXACTLY once, so a
             * var read >= 2 times is never the source of a move -- it cannot have
             * shared its buffer with another live var (e.g. `b := a` moving a's
             * buffer to b). Read >= 2 => uniquely owns its buffer => its OLD buffer
             * is truly dead on reassign. (The loop gate alone is NOT enough: `b := a`
             * outside a loop then `a = mk()` inside one would recycle b's buffer.) */
            /* ANY RHS form (call, slice, concat, copybind, array literal): a place
             * RHS is copy_into'd to a fresh buffer above, and a call/concat/literal is
             * inherently fresh, so the new value is always a DISTINCT buffer from the
             * old -- enforced at runtime by the `.data != .data` guard below. Element
             * type also unrestricted: we recycle the SPINE (data[]); heap elements are
             * separate (partial reclaim), flat structs full. */
            int do_recycle = g_loop_depth > 0 && !is_accum(s->name)
                && cv_arena(s->name) && !is_inout_param(s->name)
                && is_array(s->expr->type)
                && count_reads_b(g_proc_body, g_proc_nbody, s->name) >= 2;
            if (do_recycle) {
                /* CRITICAL ORDER: evaluate the RHS into a temp FIRST (it may read a's
                 * old buffer to build the result -- a call's arg, a slice's range, a
                 * concat operand), THEN recycle, THEN assign. The `.data != .data`
                 * guard skips the recycle in the (here impossible, but cheap to rule
                 * out) case the new value IS / points into the old buffer. */
                int tid = g_blk++;
                indent(o, ind); fprintf(o, "%s_rec%d = %s;\n", c_type(s->expr->type), tid, v);
                indent(o, ind); fprintf(o, "if (h_%s.data && h_%s.data != _rec%d.data) arena_recycle(%s, h_%s.data, (size_t)h_%s.cap * sizeof(*h_%s.data));\n",
                                        s->name, s->name, tid, owner, s->name, s->name, s->name);
                indent(o, ind); fprintf(o, "h_%s = _rec%d;\n", s->name, tid);
            } else {
                indent(o, ind);
                /* a mut param is a pointer in the body; assign through it */
                if (is_inout_param(s->name))
                    fprintf(o, "(*h_%s) = %s;\n", s->name, v);
                else
                    fprintf(o, "h_%s = %s;\n", s->name, v);
            }
            /* a non-self assignment to a tracked accumulator rebinds its
             * buffer; resync sidecars (cap 0 = the new buffer isn't ours to
             * grow in place — forces the next append to allocate). */
            if (is_accum(s->name) && s->expr->type == T_STRING && !is_inout_param(s->name))
                fprintf(o, "%*s_len_h_%s = ((const long *)h_%s)[-1]; _cap_h_%s = 0;\n",
                        ind * 4, "", s->name, s->name, s->name);
            break;
        }
        case S_INDEXSET: {
            /* the array being indexed: a variable or a struct's array field.
             * &(lvalue) gives a TychoArr* for both. */
            Expr *arrx = s->target->lhs;
            Expr *root = arrx;
            while (root->kind == E_FIELD || root->kind == E_INDEX) root = root->lhs;
            if (is_map(arrx->type)) {   /* m[k] = v  or  m[k] op= v  (#2) */
                Type vt = s->target->type;   /* the map value type */
                const char *mowner = (root->kind == E_IDENT) ? owner_arena_of(root->sval) : scope;
                int compound = (s->expr->kind == E_BINOP && s->expr->lhs == s->target);
                if (compound) {
                    /* single-eval the slot pointer: one find-or-insert, then read-modify-write
                     * through it (no double lookup). Scalar value, so a plain C operator. */
                    int id = g_blk++;
                    char *mb  = gen_lvalue(arrx, scope);
                    char *key = key_rt(arrx->type, gen_expr(s->target->rhs, scope));
                    char *rhs = gen_expr(s->expr->rhs, scope);
                    indent(o, ind);
                    /* int `/=`/`%=` route the read-modify-write through the same
                     * division guard as a plain int `/`/`%` (x/0, x%0, LONG_MIN/-1
                     * trap otherwise); other ops stay a plain C operator. */
                    if ((s->expr->op == TK_SLASH || s->expr->op == TK_PERCENT) && base_of(vt) == T_INT)
                        fprintf(o, "{ %s*_mp%d = %s(%s, &(%s), %s); *_mp%d = %s(*_mp%d, %s); }\n",
                                c_type(vt), id, map_rt(arrx->type, "slotptr"), mowner, mb, key,
                                id, s->expr->op == TK_SLASH ? "tycho_idiv" : "tycho_imod", id, rhs);
                    else if ((s->expr->op == TK_SLASH || s->expr->op == TK_PERCENT) && (base_of(vt) == T_U32 || base_of(vt) == T_U64))
                        fprintf(o, "{ %s*_mp%d = %s(%s, &(%s), %s); *_mp%d = %s(*_mp%d, %s); }\n",
                                c_type(vt), id, map_rt(arrx->type, "slotptr"), mowner, mb, key,
                                id, s->expr->op == TK_SLASH ? "tycho_udiv" : "tycho_umod", id, rhs);
                    else
                        fprintf(o, "{ %s*_mp%d = %s(%s, &(%s), %s); *_mp%d = *_mp%d %s %s; }\n",
                                c_type(vt), id, map_rt(arrx->type, "slotptr"), mowner, mb, key,
                                id, id, op_str(s->expr->op), rhs);
                } else {
                    char *lv = gen_lvalue(s->target, scope);   /* (*slotptr(owner, &m, k)) */
                    char *v  = gen_expr(s->expr, mowner);
                    if (type_is_heap(vt) && is_place(s->expr))   /* value semantics: own the bytes */
                        v = copy_into(vt, mowner, v);
                    indent(o, ind);
                    fprintf(o, "%s = %s;\n", lv, v);
                }
                break;
            }
            char *arr = gen_lvalue(arrx, scope);   /* lvalue so a nested `m[i][j]=v` indexes m's buffer */
            char *ix  = gen_expr(s->target->rhs, scope);
            char *v   = gen_expr(s->expr, scope);
            indent(o, ind);
            if (arrx->type == T_ARRAY_STRING || IS_ARRC(arrx->type)) {
                /* string/struct/array element: the set deep-copies it into the
                 * array's owning arena — the carried _ina_ arena if the root is
                 * a heap mut param. */
                const char *owner = (root->kind == E_IDENT) ? owner_arena_of(root->sval) : scope;
                fprintf(o, "tycho_arr_%s_set(%s, &(%s), %s, %s);\n", arr_fn(arrx->type), owner, arr, ix, v);
            } else if (index_in_range(s->target->lhs, s->target->rhs)) {
                fprintf(o, "(%s).data[%s] = %s;\n", arr, ix, v);   /* monotone loop index: skip the check */
            } else {   /* [int] or [float]: value word, no arena, no byte copy */
                fprintf(o, "tycho_arr_%s_set(&(%s), %s, %s);\n", arr_fn(arrx->type), arr, ix, v);
            }
            break;
        }
        case S_FIELDSET: {
            /* gen_expr(E_FIELD) is a valid C lvalue, e.g. (h_p).f_x. The
             * struct lives in its root variable's arena, so a heap field's
             * new bytes must go there too (not the current block scope, which
             * may collapse first); a heap *place* RHS is also deep-copied. */
            Expr *root = s->target;
            while (root->kind == E_FIELD || root->kind == E_INDEX || root->kind == E_TUPIDX) root = root->lhs;
            /* heap field's new bytes go in the struct's owning arena — the
             * carried _ina_ arena if the root is a heap mut param. */
            const char *owner = (root->kind == E_IDENT) ? owner_arena_of(root->sval) : scope;
            char *lv = gen_lvalue(s->target, scope);   /* projects `arr[i].f` to the element's buffer slot */
            char *v  = gen_expr(s->expr, owner);
            if (type_is_heap(s->target->type) && is_place(s->expr))
                v = copy_into(s->target->type, owner, v);
            indent(o, ind);
            fprintf(o, "%s = %s;\n", lv, v);
            break;
        }
        case S_EXPR: {
            /* MM-10: an expression statement's value is DISCARDED, so every transient
             * it allocates is dead at statement end. At function top level (scope is
             * "&_scope") there is no per-iteration reset to reclaim them, so build them
             * in a fresh per-statement `_t` arena (block-scoped, like scalar_transient)
             * and free it immediately, instead of letting them accumulate in the
             * enclosing scope until function return. Sound because stores into
             * longer-lived containers / mut route through owner_arena_of, not
             * g_cur_scope — only pure transients land in _t.
             * Gated to "&_scope" like the scalar decl/assign reclaim above: inside a
             * loop/block the scratch reset already reclaims, so the _t wrap would be a
             * redundant empty-arena per iteration. EXCLUDE or_return: it early-returns
             * past arena_free(&_t) (leak). (tychoc0's SExpr wraps at every depth; the
             * extra reclaim is output-invisible, so this tychoc-only gating — matching
             * its own decl/assign gating — keeps the fixpoint differential green.) */
            if (!strcmp(scope, "&_scope") && !expr_has_orreturn(s->expr)) {
                g_cur_scope = "&_t";
                char *v = gen_expr(s->expr, "&_t");
                indent(o, ind);
                fprintf(o, "{ Arena _t = arena_new(0); %s; arena_free(&_t); }\n", v);
                break;
            }
            char *v = gen_expr(s->expr, scope);
            indent(o, ind);
            fprintf(o, "%s;\n", v);
            break;
        }
        case S_RETURN: {
            /* push-loop fusion: a return leaves before the after-loop flush, so
             * write back every live cursor first -- the returned value (or the
             * array itself) must see the real length, not the stale descriptor. */
            for (int fi = 0; fi < g_nfuse; fi++) fuse_flush_one(o, ind, fi);
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
                if (ret_must_copy(s->expr)) {
                    char *v = gen_expr(s->expr, "&_scope");
                    indent(o, ind); fprintf(o, "{ char *_ret = tycho_str_copy(_parent, %s); %s return _ret; }\n", v, rf);
                } else {
                    char *v = gen_expr(s->expr, "_parent");
                    indent(o, ind); fprintf(o, "{ char *_ret = %s; %s return _ret; }\n", v, rf);
                }
            } else if (ret == T_ARRAY_INT) {
                /* promote up. A fresh value (literal/call) is built directly
                 * in the caller's arena; a borrowed/local variable is
                 * deep-copied into it — UNLESS the return-slot optimization
                 * already built it in _parent (then it's a no-op move). */
                if (ret_must_copy(s->expr)) {
                    char *v = gen_expr(s->expr, "&_scope");
                    indent(o, ind); fprintf(o, "{ TychoArrInt _ret = tycho_arr_int_copy(_parent, %s); %s return _ret; }\n", v, rf);
                } else {
                    char *v = gen_expr(s->expr, "_parent");
                    indent(o, ind); fprintf(o, "{ TychoArrInt _ret = %s; %s return _ret; }\n", v, rf);
                }
            } else if (ret == T_ARRAY_FLOAT) {
                /* promote up, exactly like [int] (a buffer of value words). */
                if (ret_must_copy(s->expr)) {
                    char *v = gen_expr(s->expr, "&_scope");
                    indent(o, ind); fprintf(o, "{ TychoArrFloat _ret = tycho_arr_float_copy(_parent, %s); %s return _ret; }\n", v, rf);
                } else {
                    char *v = gen_expr(s->expr, "_parent");
                    indent(o, ind); fprintf(o, "{ TychoArrFloat _ret = %s; %s return _ret; }\n", v, rf);
                }
            } else if (ret == T_ARRAY_STRING) {
                /* promote up. A fresh value (literal/split/call) is built
                 * directly in the caller's arena; a bare variable is
                 * deep-copied (buffer + every element) into it — UNLESS the
                 * return-slot optimization already built it in _parent. */
                if (ret_must_copy(s->expr)) {
                    char *v = gen_expr(s->expr, "&_scope");
                    indent(o, ind); fprintf(o, "{ TychoArrStr _ret = tycho_arr_str_copy(_parent, %s); %s return _ret; }\n", v, rf);
                } else {
                    char *v = gen_expr(s->expr, "_parent");
                    indent(o, ind); fprintf(o, "{ TychoArrStr _ret = %s; %s return _ret; }\n", v, rf);
                }
            } else if (IS_ARRC(ret)) {
                /* composite array ([Struct]/[[T]]): promote like the others; the
                 * generated copy deep-copies the buffer and every element. */
                if (ret_must_copy(s->expr)) {
                    char *v = gen_expr(s->expr, "&_scope");
                    indent(o, ind); fprintf(o, "{ %s_ret = %s; %s return _ret; }\n",
                                            c_type(ret), copy_into(ret, "_parent", v), rf);
                } else {
                    char *v = gen_expr(s->expr, "_parent");
                    indent(o, ind); fprintf(o, "{ %s_ret = %s; %s return _ret; }\n", c_type(ret), v, rf);
                }
            } else if (is_map(ret)) {
                /* promote up, exactly like the array cases: a bare map variable
                 * is only a value whose tables live in this scope, so deep-copy
                 * into the caller's arena before freeing — UNLESS the return-slot
                 * optimization already built it in _parent (then a no-op move). */
                if (ret_must_copy(s->expr)) {
                    char *v = gen_expr(s->expr, "&_scope");
                    indent(o, ind); fprintf(o, "{ %s_ret = tycho_map_%s_copy(_parent, %s); %s return _ret; }\n", c_type(ret), map_fn(ret), v, rf);
                } else {
                    char *v = gen_expr(s->expr, "_parent");
                    indent(o, ind); fprintf(o, "{ %s_ret = %s; %s return _ret; }\n", c_type(ret), v, rf);
                }
            } else if ((IS_STRUCT(ret) || IS_OPT(ret) || IS_RES(ret) || IS_TUP(ret) || IS_ENUM(ret)) && type_is_heap(ret)) {
                /* promote up. A heap struct/Option/Result/tuple built from a *place* is
                 * deep-copied into the caller's arena; a fresh literal/call is
                 * built directly there (construction re-homes any heap value).
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
            } else if (IS_FUNC(ret)) {
                /* escaping closure: re-home its captured env into the caller's
                 * arena (deep-copying heap captures) BEFORE freeing this scope,
                 * via the closure's own copyenv thunk. A plain reference has
                 * copyenv==0 and env==0, so this is a no-op for it. */
                char *v = gen_expr(s->expr, "&_scope");
                indent(o, ind);
                fprintf(o, "{ %s_ret = %s; if (_ret.env) _ret.env = _ret.copyenv(_parent, _ret.env); %s return _ret; }\n",
                        c_type(ret), v, rf);
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
            /* prong-B: no child arena for if/else blocks. Block transients fall
             * back to the enclosing `scope`, which always outlives the block, so
             * this only lengthens their lifetime (no early-free). Escaping values
             * already promote to _parent/target arena independent of any _bN.
             * Eliminates ~70% of arena_child/free churn (the if/match blocks). */
            char *c = cond_unwrap(gen_expr(s->expr, scope));
            indent(o, ind); fprintf(o, "if (%s) {\n", c);
            gen_block(o, s->body, s->nbody, ind + 1, scope, ret);
            indent(o, ind); fprintf(o, "}");
            if (s->els) {
                fprintf(o, " else {\n");
                gen_block(o, s->els, s->nels, ind + 1, scope, ret);
                indent(o, ind); fprintf(o, "}\n");
            } else {
                fprintf(o, "\n");
            }
            break;
        }
        case S_MATCH: {
            int mid = g_blk++;
            char *scrut = gen_expr(s->expr, scope);
            Type st = s->expr->type;
            indent(o, ind); fprintf(o, "{\n");
            indent(o, ind + 1); fprintf(o, "%s_m%d = %s;\n", c_type(st), mid, scrut);
            /* a `_` wildcard arm (resolver guarantees it is last) fills whichever
             * explicit branch is absent. */
            MatchArm *wildarm = NULL;
            for (int i = 0; i < s->narms; i++)
                if (!strcmp(s->arms[i].variant, "_")) wildarm = &s->arms[i];
            if (IS_OPT(st)) {
                Type inner = opt_inner(st);
                MatchArm *some = NULL, *none = NULL;
                for (int i = 0; i < s->narms; i++)
                    if (!strcmp(s->arms[i].variant, "Some")) some = &s->arms[i];
                    else if (!strcmp(s->arms[i].variant, "None")) none = &s->arms[i];
                indent(o, ind + 1); fprintf(o, "if (_m%d.has) {\n", mid);
                if (some) {
                    indent(o, ind + 2);
                    int sborrow = type_is_heap(inner)
                        && !block_mutates(some->body, some->nbody, some->binds[0]);
                    fprintf(o, "%sh_%s = %s;\n", c_type(inner), some->binds[0],
                            sborrow ? sfmt("_m%d.val", mid)
                                    : copy_into(inner, scope, sfmt("_m%d.val", mid)));
                    int m = cv_mark(); cv_push(some->binds[0], sborrow ? NULL : scope);
                    gen_block(o, some->body, some->nbody, ind + 2, scope, ret);
                    cv_restore(m);
                } else gen_block(o, wildarm->body, wildarm->nbody, ind + 2, scope, ret);
                indent(o, ind + 1); fprintf(o, "} else {\n");
                gen_block(o, none ? none->body : wildarm->body, none ? none->nbody : wildarm->nbody, ind + 2, scope, ret);
                indent(o, ind + 1); fprintf(o, "}\n");
            } else if (IS_RES(st)) {   /* Ok(x) -> .okv / Err(e) -> .errv, tag is .ok */
                Type okt = res_ok(st), errt = res_err(st);
                MatchArm *okarm = NULL, *errarm = NULL;
                for (int i = 0; i < s->narms; i++)
                    if (!strcmp(s->arms[i].variant, "Ok")) okarm = &s->arms[i];
                    else if (!strcmp(s->arms[i].variant, "Err")) errarm = &s->arms[i];
                indent(o, ind + 1); fprintf(o, "if (_m%d.ok) {\n", mid);
                if (okarm) {
                    indent(o, ind + 2);
                    int okborrow = type_is_heap(okt)
                        && !block_mutates(okarm->body, okarm->nbody, okarm->binds[0]);
                    fprintf(o, "%sh_%s = %s;\n", c_type(okt), okarm->binds[0],
                            okborrow ? sfmt("_m%d.okv", mid)
                                     : copy_into(okt, scope, sfmt("_m%d.okv", mid)));
                    int m = cv_mark(); cv_push(okarm->binds[0], okborrow ? NULL : scope);
                    gen_block(o, okarm->body, okarm->nbody, ind + 2, scope, ret);
                    cv_restore(m);
                } else gen_block(o, wildarm->body, wildarm->nbody, ind + 2, scope, ret);
                indent(o, ind + 1); fprintf(o, "} else {\n");
                if (errarm) {
                    indent(o, ind + 2);
                    int errborrow = type_is_heap(errt)
                        && !block_mutates(errarm->body, errarm->nbody, errarm->binds[0]);
                    fprintf(o, "%sh_%s = %s;\n", c_type(errt), errarm->binds[0],
                            errborrow ? sfmt("_m%d.errv", mid)
                                      : copy_into(errt, scope, sfmt("_m%d.errv", mid)));
                    int m2 = cv_mark(); cv_push(errarm->binds[0], errborrow ? NULL : scope);
                    gen_block(o, errarm->body, errarm->nbody, ind + 2, scope, ret);
                    cv_restore(m2);
                } else gen_block(o, wildarm->body, wildarm->nbody, ind + 2, scope, ret);
                indent(o, ind + 1); fprintf(o, "}\n");
            } else {   /* IS_ENUM: a tag dispatch; each arm binds its payload */
                EnumDef *ed = &g_enums[ENUM_ID(st)];
                const char *en = ed->name;
                for (int i = 0; i < s->narms; i++) {
                    MatchArm *arm = &s->arms[i];
                    if (!strcmp(arm->variant, "_")) continue;   /* the catch-all is the trailing else, below */
                    int vi = 0;
                    for (int v = 0; v < ed->nvariants; v++)
                        if (!strcmp(ed->variants[v].name, arm->variant)) { vi = v; break; }
                    Variant *var = &ed->variants[vi];
                    indent(o, ind + 1);
                    fprintf(o, "%sif (_m%d->tag == %d) {\n", i ? "else " : "", mid, vi);
                    int bid = g_blk++;   /* names the payload pointer _p<bid> */
                    int m = cv_mark();
                    if (var->npayload > 0) {
                        indent(o, ind + 2);
                        fprintf(o, "E_%s_%s *_p%d = &_m%d->u.%s;\n",
                                en, var->name, bid, mid, var->name);
                        for (int b = 0; b < arm->nbinds; b++) {
                            char *field = sfmt("_p%d->f%d", bid, b);
                            /* borrow the scrutinee's payload instead of deep-
                             * copying it, unless this binding is mutated in
                             * place in the arm (which would reach through). */
                            int borrow = type_is_heap(var->payload[b])
                                && !block_mutates(arm->body, arm->nbody, arm->binds[b]);
                            indent(o, ind + 2);
                            fprintf(o, "%sh_%s = %s;\n", c_type(var->payload[b]), arm->binds[b],
                                    borrow ? field : copy_into(var->payload[b], scope, field));
                            /* a borrowed binding owns no arena (like a param):
                             * NULL keeps move-on-last-use from handing it off. */
                            cv_push(arm->binds[b], borrow ? NULL : scope);
                        }
                    }
                    gen_block(o, arm->body, arm->nbody, ind + 2, scope, ret);
                    cv_restore(m);
                    indent(o, ind + 1); fprintf(o, "}\n");
                }
                /* Exhaustiveness is enforced at resolve time (every variant has
                 * an arm, no wildcard), so this else is unreachable. Emit it as a
                 * trap anyway: the generated C then provably returns on every
                 * path (silencing -Wreturn-type, which the C compiler emits
                 * because it cannot see the tag dispatch is exhaustive), and any
                 * future non-exhaustive match aborts cleanly instead of silently
                 * falling through. */
                if (wildarm) {   /* `_` catch-all: covers every unlisted variant */
                    indent(o, ind + 1); fprintf(o, "else {\n");
                    gen_block(o, wildarm->body, wildarm->nbody, ind + 2, scope, ret);
                    indent(o, ind + 1); fprintf(o, "}\n");
                } else {
                    indent(o, ind + 1);
                    fprintf(o, "else { fprintf(stderr, \"tycho: non-exhaustive match\\n\"); exit(1); }\n");
                }
            }
            indent(o, ind); fprintf(o, "}\n");
            break;
        }
        case S_CONST: break;   /* a const is folded at each use; it emits no runtime storage */
        case S_BREAK:
        case S_CONTINUE: {
            if (g_loop_depth == 0)
                die_at(s->line, "'%s' outside a loop", s->kind == S_BREAK ? "break" : "continue");
            /* if/match blocks open no arena (they share the loop's scratch), and
             * the loop arena is arena_reset at the top of each iteration and
             * arena_free'd once after the loop -- so a bare C break/continue
             * reclaims correctly with no extra cleanup. Tasks are the exception
             * (CC-2): a handle declared since this loop's body entry must be
             * finished here, because the jump skips the block-end finishes. */
            { int tmark = g_loop_taskmark[g_loop_depth - 1];
              if (g_ntaskvars > tmark) { indent(o, ind); fprintf(o, "%s\n", task_finishes_from(tmark)); } }
            indent(o, ind); fprintf(o, "%s;\n", s->kind == S_BREAK ? "break" : "continue");
            break;
        }
        case S_WHILE: {
            int id = g_blk++;
            indent(o, ind); fprintf(o, "{\n");
            indent(o, ind + 1); fprintf(o, "Arena _scr%d = arena_child(%s);\n", id, scope);
            int _fo = fuse_open(o, s->body, s->nbody, ind + 1, s->expr);
            char *c = cond_unwrap(gen_expr(s->expr, scope));
            indent(o, ind + 1); fprintf(o, "while (%s) {\n", c);
            indent(o, ind + 2); fprintf(o, "arena_reset(&_scr%d);\n", id);
            char *ss = sfmt("&_scr%d", id);
            ascope_push(ss);   /* a return inside the loop must free _scrN too */
            g_loop_taskmark[g_loop_depth] = g_ntaskvars;   /* break/continue finish tasks above this */
            g_loop_depth++;    /* moves are unsafe inside a loop (single read runs N times) */
            gen_block(o, s->body, s->nbody, ind + 2, ss, ret);
            g_loop_depth--;
            g_nascope--;
            indent(o, ind + 1); fprintf(o, "}\n");
            fuse_close(o, _fo, ind + 1);   /* break falls through to here; flush the cursors */
            indent(o, ind + 1); fprintf(o, "arena_free(&_scr%d);\n", id);
            indent(o, ind); fprintf(o, "}\n");
            break;
        }
        case S_SELECT: {   /* CC-5: try every recv arm; closed when ALL drain; else default or pause+retry.
                            * A goto loop (not for(;;)) so a user break/continue in an arm body binds to
                            * the USER'S enclosing loop, never to select's own retry machinery. */
            int id = g_blk++;
            indent(o, ind); fprintf(o, "{\n");
            for (int i = 0; i < s->narms; i++)
                if (s->sel_ch[i]) {   /* evaluate each channel expression exactly once */
                    indent(o, ind + 1);
                    fprintf(o, "HChan *_sc%d_%d = %s;\n", id, i, gen_expr(s->sel_ch[i], scope));
                }
            indent(o, ind + 1); fprintf(o, "int _ssp%d = 0, _open%d = 0;\n", id, id);
            indent(o, ind + 1); fprintf(o, "_sel_retry_%d: ;\n", id);
            indent(o, ind + 1); fprintf(o, "_open%d = 0;\n", id);
            for (int i = 0; i < s->narms; i++) {
                MatchArm *a = &s->arms[i];
                if (!s->sel_ch[i]) continue;
                Type it = chan_inner(s->sel_ch[i]->type);
                indent(o, ind + 1);
                fprintf(o, "{ %s_v; int _st = tycho_chan_tryrecv_%d(_sc%d_%d, %s, &_v); if (_st == 1) {\n",
                        c_type(it), CHAN_ID(s->sel_ch[i]->type), id, i, scope);
                indent(o, ind + 2); fprintf(o, "%sh_%s = _v;\n", c_type(it), a->binds[0]);
                int m = cv_mark();
                cv_push(a->binds[0], scope);
                gen_block(o, a->body, a->nbody, ind + 2, scope, ret);
                cv_restore(m);
                indent(o, ind + 2); fprintf(o, "goto _sel_done_%d;\n", id);
                indent(o, ind + 1); fprintf(o, "} if (_st == 0) _open%d = 1; }\n", id);
            }
            indent(o, ind + 1); fprintf(o, "if (!_open%d) {\n", id);
            for (int i = 0; i < s->narms; i++)
                if (!strcmp(s->arms[i].variant, "closed"))
                    gen_block(o, s->arms[i].body, s->arms[i].nbody, ind + 2, scope, ret);
            indent(o, ind + 2); fprintf(o, "goto _sel_done_%d;\n", id);
            indent(o, ind + 1); fprintf(o, "}\n");
            int has_def = 0;
            for (int i = 0; i < s->narms; i++)
                if (!strcmp(s->arms[i].variant, "default")) {
                    has_def = 1;
                    gen_block(o, s->arms[i].body, s->arms[i].nbody, ind + 1, scope, ret);
                    indent(o, ind + 1); fprintf(o, "goto _sel_done_%d;\n", id);
                }
            if (!has_def) {
                indent(o, ind + 1); fprintf(o, "tycho_select_pause(&_ssp%d);\n", id);
                indent(o, ind + 1); fprintf(o, "goto _sel_retry_%d;\n", id);
            }
            indent(o, ind + 1); fprintf(o, "_sel_done_%d: ;\n", id);
            indent(o, ind); fprintf(o, "}\n");
            break;
        }
        case S_FORRANGE: {
            if (s->parallel) { gen_parfor(o, s, ind, scope); break; }   /* CC-3 */
            int id = g_blk++;
            char *start = gen_expr(s->r_start, scope);
            char *stop  = gen_expr(s->r_stop,  scope);
            char *step  = s->r_step ? gen_expr(s->r_step, scope) : sfmt("1L");
            char *ss = sfmt("&_scr%d", id);
            indent(o, ind); fprintf(o, "{\n");
            indent(o, ind + 1); fprintf(o, "Arena _scr%d = arena_child(%s);\n", id, scope);
            int _fo = fuse_open(o, s->body, s->nbody, ind + 1, NULL);   /* bounds eval once, pre-loop */
            indent(o, ind + 1); fprintf(o, "long _stop%d = %s, _step%d = %s;\n", id, stop, id, step);
            indent(o, ind + 1);
            fprintf(o, "for (long h_%s = %s; _step%d > 0 ? h_%s < _stop%d : h_%s > _stop%d; h_%s += _step%d) {\n",
                    s->name, start, id, s->name, id, s->name, id, s->name, id);
            indent(o, ind + 2); fprintf(o, "arena_reset(&_scr%d);\n", id);
            int m = cv_mark();
            cv_push(s->name, ss);   /* loop var is an int value; owner is irrelevant but tracked */
            ascope_push(ss);        /* a return inside the loop must free _scrN too */
            g_loop_taskmark[g_loop_depth] = g_ntaskvars;   /* break/continue finish tasks above this */
            g_loop_depth++;         /* moves are unsafe inside a loop (single read runs N times) */
            /* bounds-check elision: `for i in range(len(A)):` proves A[i] in-range
             * for the body, provided the body never reassigns/shadows A or i and
             * never passes A whole to a call (see stmt_unsafe). */
            int elide_pushed = 0;
            if (elision_on() && s->r_start->kind == E_INT && s->r_start->ival == 0 &&
                s->r_step == NULL && s->r_stop->kind == E_CALL && s->r_stop->sval &&
                !strcmp(s->r_stop->sval, "len") && s->r_stop->nargs == 1 &&
                s->r_stop->args[0]->kind == E_IDENT && g_nelide < 64 &&
                !stmts_unsafe(s->body, s->nbody, s->name, s->r_stop->args[0]->sval)) {
                g_elide[g_nelide].iv = s->name;
                g_elide[g_nelide].arr = s->r_stop->args[0]->sval;
                g_nelide++; elide_pushed = 1;
            }
            gen_block(o, s->body, s->nbody, ind + 2, ss, ret);
            if (elide_pushed) g_nelide--;
            g_loop_depth--;
            g_nascope--;
            cv_restore(m);
            indent(o, ind + 1); fprintf(o, "}\n");
            fuse_close(o, _fo, ind + 1);   /* break falls through to here; flush the cursors */
            indent(o, ind + 1); fprintf(o, "arena_free(&_scr%d);\n", id);
            indent(o, ind); fprintf(o, "}\n");
            break;
        }
    }
}

static void gen_block(FILE *o, Stmt **body, int n, int ind,
                      const char *scope, Type ret) {
    int m = cv_mark();
    int tm = g_ntaskvars;
    for (int i = 0; i < n; i++) gen_stmt(o, body[i], ind, scope, ret);
    if (g_ntaskvars > tm) {   /* CC-2 implicit join: tasks declared in this block die here
                               * (after a trailing return this is dead C -- the return path
                               * already finished them via return_frees) */
        indent(o, ind); fprintf(o, "%s\n", task_finishes_from(tm));
        g_ntaskvars = tm;
    }
    cv_restore(m);   /* variables declared in this block go out of scope */
}

static void gen_signature(FILE *o, Proc *pr) {
    fprintf(o, "%sh_%s(Arena *_parent", c_type(pr->ret), pr->name);
    for (int i = 0; i < pr->nparams; i++) {
        /* mut params are received by pointer so writes reach the caller's
         * storage (copy-in copy-out, realized as call-by-reference). A HEAP
         * mut additionally carries its value's owning arena (_ina_<name>),
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

/* FFI: forward-declare the C symbol with its real C ABI — no arena, no h_
 * prefix. This is enough to call it; no header #include needed. */
static void gen_extern_proto(FILE *o, Proc *pr) {
    /* bytes crosses as (ptr,len): a `bytes` parameter becomes two C args
     * (const unsigned char*, long); a `bytes` return becomes a void function with
     * two trailing out-params (unsigned char** out, long* outlen) — the out-param
     * shim convention. */
    int bret = (pr->ret == T_BYTES);
    /* FFI R3a: an `Option(string)` return is a char* at the C ABI (the wrapper at
     * the call site turns NULL into None); declare the real C return, not TychoOpt. */
    int optstr = (IS_OPT(pr->ret) && opt_inner(pr->ret) == T_STRING);
    fprintf(o, "extern %s%s(", bret ? "void " : optstr ? "char *" : pr->ret_ffi_ct ? pr->ret_ffi_ct : c_type(pr->ret), pr->name);
    int emitted = 0;
    for (int i = 0; i < pr->nparams; i++) {
        if (pr->params[i].type == T_BYTES)
            fprintf(o, "%sconst unsigned char *, long", emitted++ ? ", " : "");
        else if (pr->params[i].is_inout)   /* FFI R4: out-param — the C fn takes a pointer to T */
            fprintf(o, "%s%s*", emitted++ ? ", " : "", c_type(pr->params[i].type));
        else
            fprintf(o, "%s%s", emitted++ ? ", " : "", pr->params[i].ffi_ct ? pr->params[i].ffi_ct : c_type(pr->params[i].type));
    }
    if (bret) fprintf(o, "%sunsigned char **, long *", emitted++ ? ", " : "");
    if (emitted == 0) fprintf(o, "void");
    fprintf(o, ");\n");
}

static void gen_proc(FILE *o, Proc *pr) {
    gen_signature(o, pr);
    fprintf(o, " {\n");
    indent(o, 1); fprintf(o, "Arena _scope = arena_child(_parent);\n");
    g_gen_ret = pr->ret;
    g_proc_body = pr->body; g_proc_nbody = pr->nbody;   /* for move-on-last-use read counts */
    g_loop_depth = 0;
    g_ncv = 0;
    g_nascope = 0;   /* no enclosing block arenas at the proc body top level */
    g_ntaskvars = 0; /* CC-2: no live tasks at proc entry */
    /* return-slot optimization: which top-level locals escape via return */
    g_nesc = 0;
    collect_escapes(pr->body, pr->nbody);
    /* in-place append: which string locals are self-append accumulators */
    g_naccum = 0;
    collect_accums(pr->body, pr->nbody);
    /* the string accumulator opt declares its sidecar len/cap locals at the
     * variable's S_DECL; a by-value parameter has no S_DECL, so a self-append on
     * a string param (`s = s + e`) must NOT take the in-place path — its
     * sidecars would be undeclared C. Drop NON-mut params from the accumulator
     * set; they fall back to ordinary concat/pure-set-and-rebind, which is
     * correct. A mut param is KEPT: it carries no string sidecars (mut
     * string is forbidden, so the only mut accumulator is a map), and its
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
    /* register this proc's mut params so the body derefs them as (*h_x) */
    g_ninout = 0;
    g_nheap_inout = 0;
    for (int i = 0; i < pr->nparams; i++)
        if (pr->params[i].is_inout) {
            g_inout[g_ninout++] = pr->params[i].name;
            if (type_is_heap(pr->params[i].type))
                g_heap_inout[g_nheap_inout++] = pr->params[i].name;
        }
    /* register ALL params so can_move_from never hands off a param's buffer */
    g_nparam = 0;
    for (int i = 0; i < pr->nparams && i < 16; i++) g_param_sink[i] = pr->params[i].is_sink;
    for (int i = 0; i < pr->nparams; i++)
        g_param[g_nparam++] = pr->params[i].name;
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
    /* Every parameter borrows the caller's bytes; its value does NOT live in
     * this proc's _parent. (An earlier optimization marked a returned string
     * param as _parent to skip the deep copy at return -- UNSOUND: the return
     * value outlives the call, but the caller frequently passes the arg in a
     * transient arena it frees right after the call, leaving the returned
     * pointer dangling. Surfaced by self-hosting tychoc0's resolve_nt, which
     * returns its `ty` param. So: never mark a param _parent; `return param`
     * deep-copies into _parent like any other borrowed place.) */
    for (int i = 0; i < pr->nparams; i++)
        cv_push(pr->params[i].name, "&_scope");
    /* Structs are passed by value, but C copies them shallowly — a heap field
     * (string/array) still points at the caller's bytes. Deep-copy heap-
     * bearing struct params into this scope so the parameter is a truly
     * independent value: mutating its array field cannot touch the caller, and
     * the copy is owned here. ([int]/[string] params stay read-only borrows;
     * string params are immutable, so neither needs this.) */
    for (int i = 0; i < pr->nparams; i++) {
        Type pt = pr->params[i].type;
        /* a mut struct is a pointer to the caller's value — must NOT be
         * deep-copied (the whole point is to mutate the caller's). Only
         * by-value heap struct params are copied for independence.
         *
         * ...and only when the body actually MUTATES the param. A read-only
         * heap struct param (the common case — e.g. the Ctx symbol table
         * threaded through nearly every function) can borrow the caller's
         * value directly: the caller outlives the call, the shallow C-by-value
         * copy aliases the caller's heap fields, and with no mutation that
         * aliasing is unobservable. A `return param` still deep-copies into
         * _parent via the return path, so borrowing never dangles. This is the
         * same borrow-iff-not-mutated rule already used for match-arm payloads.
         * Eliminates the per-call full-context deep copy (gprof: 72.7k
         * tycho_copy_S_Ctx, the dominant residual cost). */
        if (IS_STRUCT(pt) && type_is_heap(pt) && !pr->params[i].is_inout
            && block_mutates(pr->body, pr->nbody, pr->params[i].name)) {
            indent(o, 1);
            fprintf(o, "h_%s = tycho_copy_S_%s(&_scope, h_%s);\n",
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
            indent(o, 1); fprintf(o, "arena_free(&_scope); return (%s){0};\n", c_type(pr->ret));
        }
    }
    fprintf(o, "}\n\n");
}

/* Struct bodies and Option typedefs embed their members BY VALUE, so they must
 * be emitted in containment order (composite-array typedefs are emitted before
 * this — they hold only pointers, so they break cycles). A DFS with colouring
 * (0 unvisited, 1 on-stack, 2 done) emits each in dependency order; a back-edge
 * is an infinite type (a struct that contains itself by value), which is a real
 * error — use an array or `Option([T])` for indirection. */
static int *g_struct_color; static int g_struct_color_cap;
static int *g_opt_color;    static int g_opt_color_cap;
static int *g_res_color;    static int g_res_color_cap;
static int *g_tup_color;    static int g_tup_color_cap;
static int g_emit_line;

static void emit_aggregate(FILE *o, Type t) {
    if (has_typaram(t)) return;   /* generics: a type mentioning `$T` (from a template) is transient -- never emitted */
    if (IS_STRUCT(t)) {
        int id = STRUCT_ID(t);
        if (g_structs[id].generic) return;   /* generics: a `$T` template emits no C; its instances do */
        if (g_struct_color[id] == 2) return;
        if (g_struct_color[id] == 1)
            die_at(g_emit_line, "infinite type: %s contains itself by value — "
                   "use an array ([%s]) or Option([%s]) for indirection",
                   g_structs[id].name, g_structs[id].name, g_structs[id].name);
        g_struct_color[id] = 1;
        int save = g_emit_line; g_emit_line = g_structs[id].line;
        StructDef *sd = &g_structs[id];
        for (int j = 0; j < sd->nfields; j++) {
            Type ft = sd->fields[j].type;
            if (IS_STRUCT(ft) || IS_OPT(ft) || IS_RES(ft) || IS_TUP(ft)) emit_aggregate(o, ft);
        }
        g_emit_line = save;
        g_struct_color[id] = 2;
        if (o) {   /* o == NULL: a pure infinite-type check, no emit */
            fprintf(o, "struct S_%s_ {\n", sd->name);
            for (int j = 0; j < sd->nfields; j++)
                fprintf(o, "    %sf_%s;\n", c_type(sd->fields[j].type), sd->fields[j].name);
            fprintf(o, "};\n");
        }
    } else if (IS_OPT(t)) {
        int id = OPT_ID(t);
        if (g_opt_color[id] == 2) return;
        if (g_opt_color[id] == 1)
            die_at(g_emit_line, "infinite type: an Option contains itself by value — "
                   "use Option([T]) for indirection");
        g_opt_color[id] = 1;
        Type inner = g_opttypes[id].inner;
        if (IS_STRUCT(inner) || IS_OPT(inner) || IS_RES(inner) || IS_TUP(inner)) emit_aggregate(o, inner);
        g_opt_color[id] = 2;
        if (o) fprintf(o, "struct TychoOpt%d_ { char has; %sval; };\n", id, c_type(inner));
    } else if (IS_RES(t)) {   /* embeds both inner types by value (the inactive one zeroed) */
        int id = RES_ID(t);
        if (g_res_color[id] == 2) return;
        if (g_res_color[id] == 1)
            die_at(g_emit_line, "infinite type: a Result contains itself by value — "
                   "use indirection (e.g. Result([T], E))");
        g_res_color[id] = 1;
        Type okt = g_restypes[id].ok, errt = g_restypes[id].err;
        if (IS_STRUCT(okt)  || IS_OPT(okt)  || IS_RES(okt)  || IS_TUP(okt))  emit_aggregate(o, okt);
        if (IS_STRUCT(errt) || IS_OPT(errt) || IS_RES(errt) || IS_TUP(errt)) emit_aggregate(o, errt);
        g_res_color[id] = 2;
        if (o) fprintf(o, "struct TychoRes%d_ { char ok; %sokv; %serrv; };\n",
                       id, c_type(okt), c_type(errt));
    } else {   /* IS_TUP: embeds every element by value */
        int id = TUP_ID(t);
        if (g_tup_color[id] == 2) return;
        if (g_tup_color[id] == 1)
            die_at(g_emit_line, "infinite type: a tuple contains itself by value");
        g_tup_color[id] = 1;
        TupType *tt = &g_tuptypes[id];
        for (int j = 0; j < tt->n; j++) {
            Type et = tt->elems[j];
            if (IS_STRUCT(et) || IS_OPT(et) || IS_RES(et) || IS_TUP(et)) emit_aggregate(o, et);
        }
        g_tup_color[id] = 2;
        if (o) {
            fprintf(o, "struct TychoTup%d_ {", id);
            for (int j = 0; j < tt->n; j++) fprintf(o, " %s_%d;", c_type(tt->elems[j]), j);
            fprintf(o, " };\n");
        }
    }
}

/* Run the DFS purely to reject infinite (by-value self-containing) types, BEFORE
 * the resolver runs — type_is_heap recurses through fields and would otherwise
 * loop forever on such a type. */
static void check_finite_types(void) {
    TBL_RESERVE(g_struct_color, g_nstructs,  g_struct_color_cap);
    TBL_RESERVE(g_opt_color,    g_nopttypes, g_opt_color_cap);
    TBL_RESERVE(g_res_color,    g_nrestypes, g_res_color_cap);
    TBL_RESERVE(g_tup_color,    g_ntuptypes, g_tup_color_cap);
    for (int i = 0; i < g_nstructs; i++)  g_struct_color[i] = 0;
    for (int i = 0; i < g_nopttypes; i++) g_opt_color[i] = 0;
    for (int i = 0; i < g_nrestypes; i++) g_res_color[i] = 0;
    for (int i = 0; i < g_ntuptypes; i++) g_tup_color[i] = 0;
    g_emit_line = 0;
    for (int i = 0; i < g_nstructs; i++)  emit_aggregate(NULL, STRUCT_TYPE(i));
    for (int i = 0; i < g_nopttypes; i++) emit_aggregate(NULL, T_OPT_BASE + i);
    for (int i = 0; i < g_nrestypes; i++) emit_aggregate(NULL, T_RES_BASE + i);
    for (int i = 0; i < g_ntuptypes; i++) emit_aggregate(NULL, T_TUP_BASE + i);
}

/* Materialize a generic instance as an ordinary Proc: the template's body
 * (shared), the template's parameter names, but the instance's concrete types.
 * Used both to emit its prototype and to resolve+emit its body. */
/* ---- Generics Stage-2: deep-clone a template body per instance ----
 * Every child pointer is deep-copied so two instances never share a mutable
 * node (the prior bug: re-resolving one shared body leaked types across
 * instances). `$T` is substituted only where a type appears as SOURCE (an
 * explicit `[]$T` element type, a `x : $T` annotation, explicit call type-args);
 * resolver-filled fields (Expr.type, Stmt.decl_type/mtypes) stay at the
 * template's pristine unresolved values and are filled fresh per instance.
 * Strings (sval/name/pkg/qual/variant) are immutable and shared. */
static Expr *clone_expr(Expr *e, Type *binds);
static Stmt *clone_stmt(Stmt *s, Type *binds);

static Expr **clone_exprs(Expr **a, int n, Type *binds) {
    if (n == 0) return NULL;
    Expr **out = (Expr **)xmalloc((size_t)n * sizeof(Expr *));
    for (int i = 0; i < n; i++) out[i] = clone_expr(a[i], binds);
    return out;
}

/* Generic instantiation clones the template body BEFORE it is resolved, so a deep
 * expression in a generic body skips the resolve_expr guard. clone_expr recurses
 * on lhs/rhs/args, so guard it too (NULL handled here so e->line is always valid). */
static int g_clone_depth = 0;
static Expr *clone_expr_inner(Expr *e, Type *binds);
static Expr *clone_expr(Expr *e, Type *binds) {
    if (!e) return NULL;
    if (++g_clone_depth > TYCHO_MAX_TREE_DEPTH) die_at(e->line, "expression too deeply nested to instantiate (max %d)", TYCHO_MAX_TREE_DEPTH);
    Expr *c = clone_expr_inner(e, binds);
    g_clone_depth--;
    return c;
}
static Expr *clone_expr_inner(Expr *e, Type *binds) {
    Expr *c = (Expr *)xmalloc(sizeof(Expr));
    *c = *e;
    c->lhs  = clone_expr(e->lhs, binds);
    c->rhs  = clone_expr(e->rhs, binds);
    c->args = clone_exprs(e->args, e->nargs, binds);
    if (e->kind == E_ARRLIT && has_typaram((Type)e->ival))   /* `[]$T` element type */
        c->ival = (long)subst_type((Type)e->ival, binds);
    if (e->ntypeargs > 0) {                                   /* explicit `f$(T, ...)` type args */
        c->typeargs = (Type *)xmalloc((size_t)e->ntypeargs * sizeof(Type));
        for (int i = 0; i < e->ntypeargs; i++) c->typeargs[i] = subst_type(e->typeargs[i], binds);
    }
    return c;
}

static Stmt **clone_block(Stmt **body, int n, Type *binds) {
    if (n == 0) return NULL;
    Stmt **out = (Stmt **)xmalloc((size_t)n * sizeof(Stmt *));
    for (int i = 0; i < n; i++) out[i] = clone_stmt(body[i], binds);
    return out;
}

static Stmt *clone_stmt(Stmt *s, Type *binds) {
    if (!s) return NULL;
    Stmt *c = (Stmt *)xmalloc(sizeof(Stmt));
    *c = *s;
    c->ctrl    = clone_stmt(s->ctrl, binds);   /* value if/match on a `:=` decl (ROADMAP 2.1) */
    c->expr    = clone_expr(s->expr, binds);
    c->target  = clone_expr(s->target, binds);
    c->r_start = clone_expr(s->r_start, binds);
    c->r_stop  = clone_expr(s->r_stop, binds);
    c->r_step  = clone_expr(s->r_step, binds);
    c->body = clone_block(s->body, s->nbody, binds);
    c->els  = clone_block(s->els, s->nels, binds);
    if (s->typed_decl && has_typaram(s->annot))   /* `x : $T = ...` annotation */
        c->annot = subst_type(s->annot, binds);
    if (s->narms > 0) {
        c->arms = (MatchArm *)xmalloc((size_t)s->narms * sizeof(MatchArm));
        for (int i = 0; i < s->narms; i++) {
            c->arms[i] = s->arms[i];                          /* variant + binds[] strings shared */
            c->arms[i].body = clone_block(s->arms[i].body, s->arms[i].nbody, binds);
        }
    }
    if (s->sel_ch && s->narms > 0) {                          /* S_SELECT per-arm channel exprs */
        c->sel_ch = (Expr **)xmalloc((size_t)s->narms * sizeof(Expr *));
        for (int i = 0; i < s->narms; i++) c->sel_ch[i] = clone_expr(s->sel_ch[i], binds);
    }
    return c;
}

static Proc *ginst_to_proc(GInst *gi) {
    Proc *p = (Proc *)xmalloc(sizeof(Proc));
    *p = *gi->tmpl;
    p->name = gi->name; p->ret = gi->ret; p->generic = 0; p->is_extern = 0;
    p->params = (Param *)xmalloc((size_t)gi->nparams * sizeof(Param));
    for (int j = 0; j < gi->nparams; j++) {
        p->params[j] = gi->tmpl->params[j];   /* same name + mut flag */
        p->params[j].type = gi->params[j];     /* bound concrete type */
    }
    p->nparams = gi->nparams;
    p->body = gi->body; p->nbody = gi->nbody;   /* Stage-2: resolve+emit the instance's OWN cloned body */
    return p;
}

static void gen_program(FILE *o, ProcVec *prog) {
    fputs(TYCHO_RUNTIME, o);
    fputs("\n/* ---- generated from Tycho source ---- */\n\n", o);
    /* Stage-2 (#3): resolve every generic instance body to a fixpoint UP FRONT,
     * before any emission, so a generic body that calls another generic
     * discovers + interns the nested instance now — it then gets both a
     * prototype and a body below. The loop re-reads g_nginsts as nested
     * instances append; each resolved Proc is kept for the emit loops. gen_proc
     * is self-contained, so resolve (here) and emit (below) can be separated. */
    g_ninst_procs = 0;
    for (int i = 0; i < g_nginsts; i++) {
        Proc *p = ginst_to_proc(&g_ginsts[i]);
        g_nvars = 0;
        for (int j = 0; j < p->nparams; j++) {
            Type pt = p->params[j].type;
            int mutable = (!is_array(pt) && !is_map(pt) && !IS_SOA(pt)) || p->params[j].is_inout || p->params[j].is_sink;
            vars_push(p->params[j].name, pt, mutable);
        }
        g_fn_ret = p->ret; g_dup_base = 0;
        resolve_block(p->body, p->nbody, p->ret);
        TBL_ENSURE(g_inst_procs, g_ninst_procs, g_inst_procs_cap);
        g_inst_procs[g_ninst_procs++] = p;
    }
    /* Types reference one another, sometimes cyclically (a `[Node]` field is a
     * TychoArrC descriptor holding S_Node*). Emit in dependency layers:
     *   1. forward-declare every struct tag,
     *   2. composite-array typedefs (each holds only a pointer + longs, so a
     *      struct element needs just its tag from step 1),
     *   3. struct bodies AND Option typedefs, topologically by by-value
     *      containment (a struct/Option embeds its members by value; the arrays
     *      above, being pointers, already broke any cycle),
     *   4. prototypes for the generated copy/eq + array/Option ops — this breaks
     *      the copy<->copy recursion (a struct's copy calls its array field's
     *      copy, which calls the element struct's copy),
     *   5-7. the function bodies. */
    for (int i = 0; i < g_nstructs; i++)            /* (1) struct + Option tags, so */
        fprintf(o, "typedef struct S_%s_ S_%s;\n", g_structs[i].name, g_structs[i].name);
    for (int i = 0; i < g_nopttypes; i++)           /* a pointer to either resolves */
        fprintf(o, "typedef struct TychoOpt%d_ TychoOpt%d;\n", i, i);
    for (int i = 0; i < g_nrestypes; i++)           /* Result tags too */
        fprintf(o, "typedef struct TychoRes%d_ TychoRes%d;\n", i, i);
    for (int i = 0; i < g_ntuptypes; i++)           /* tuple tags too */
        fprintf(o, "typedef struct TychoTup%d_ TychoTup%d;\n", i, i);
    fputs("\n", o);
    /* (2) enum descriptors FIRST: a fixed { tag, ptr }, depends on nothing. They
     * must precede the composite-array typedefs below, because a `[Enum]` array
     * holds `E_Foo *data` and E_Foo is an anonymous-struct typedef that cannot
     * be forward-declared (unlike the struct/Option/Result/tuple tags above) —
     * so it has to be a complete type at the point the array typedef uses it.
     * This is the recursive-enum-with-array-of-itself case (e.g. an AST node
     * `enum Stmt: ... SIf(Expr, [Stmt], [Stmt])`). */
    for (int i = 0; i < g_nenums; i++) {            /* forward-declare cells; a value is E_<name>* */
        /* Templates included (mirrors the struct tag loop above): a recursive generic
         * enum with an array payload (`enum Tree($T): Node([Tree($T)])`) interns a dead
         * template array composite `TychoArrC<n> { E_Tree **data; }` that names the
         * template cell as an incomplete POINTER. Without this forward typedef cc errors
         * `unknown type name 'E_Tree'`. The body/payload/copy/eq loops below still skip
         * generics, so the template stays an incomplete type — never completed, never
         * instantiated (only its E_Tree__int instances are). */
        fprintf(o, "typedef struct E_%s E_%s;\n", g_enums[i].name, g_enums[i].name);
    }
    for (int i = 0; i < g_narrtypes; i++)           /* forward-declare composite-array/map tags so a fn value */
        fprintf(o, "typedef struct TychoArrC%d_ TychoArrC%d;\n", i, i);   /* whose param/ret is one (fn([T])->R) can name it incomplete */
    for (int i = 0; i < g_nmaptypes; i++)
        fprintf(o, "typedef struct TychoMapC%d_ TychoMapC%d;\n", i, i);
    /* (2a') function-value typedefs FIRST — a container may now hold a fn value
     * (array elem / struct field / map value), so FnC<id> must be complete before
     * the composite-array/struct bodies that embed it. A fn(P...)->R value is a
     * 3-word FAT POINTER {env, call, copyenv}; common param/ret types (scalars,
     * strings, scalar arrays) are complete here. */
    for (int i = 0; i < g_nfunctypes; i++) {
        if (has_typaram(T_FUNC_BASE + i)) continue;   /* a `fn($T)->...` lives only in a generic template; instances use a concrete fn type. Emitting it would be invalid C (a `void` param). */
        FuncTy *f = &g_functypes[i];
        fprintf(o, "typedef struct { void *env; %s(*call)(void*, Arena*", c_type(f->ret));
        for (int j = 0; j < f->n; j++) fprintf(o, ", %s", c_type(f->params[j]));
        fprintf(o, "); void *(*copyenv)(Arena*, void*); } FnC%d;\n", i);   /* copyenv re-homes the captured env on return (0 for a plain ref) */
    }
    if (g_nfunctypes) fputs("\n", o);
    for (int i = 0; i < g_narrtypes; i++)           /* (2b) composite-array bodies (tags forward-declared above) */
        fprintf(o, "struct TychoArrC%d_ { %s*data; long len; long cap; };\n",
                i, c_type(g_arrtypes[i].elem));
    for (int i = 0; i < g_nmaptypes; i++)           /* (2b') composite-map bodies [K: V] */
        if (mapkey_composite(g_maptypes[i].key))    /* struct keys: stored by value, deep hash/eq, occupancy array */
            fprintf(o, "struct TychoMapC%d_ { %s*keys; %s*vals; unsigned char *occ; long len; long cap; long used; long *nxt; long *prv; long head; long tail; };\n",
                    i, c_type(g_maptypes[i].key), c_type(g_maptypes[i].val));
        else if (mapkey_intrep(g_maptypes[i].key))  /* int(-rep) keys incl. enum tags: occupancy array (0 is a real key) */
            fprintf(o, "struct TychoMapC%d_ { long *keys; %s*vals; unsigned char *occ; long len; long cap; long used; long *nxt; long *prv; long head; long tail; };\n",
                    i, c_type(g_maptypes[i].val));
        else
            fprintf(o, "struct TychoMapC%d_ { char **keys; %s*vals; long len; long cap; long used; long *nxt; long *prv; long head; long tail; };\n",
                    i, c_type(g_maptypes[i].val));
    /* (2c) soa typedefs: one field-buffer POINTER per struct field + len/cap.
     * Members are pointers, so the element struct's tag forward-decl above is
     * enough — this can precede struct bodies, letting a struct embed a soa by
     * value. The push/copy/eq BODIES (which need sizeof field types) stay later. */
    for (int i = 0; i < g_nsoatypes; i++) {
        StructDef *sd = &g_structs[STRUCT_ID(g_soatypes[i].st)];
        fprintf(o, "typedef struct {");
        for (int f = 0; f < sd->nfields; f++)
            fprintf(o, " %s*f%d;", c_type(sd->fields[f].type), f);
        fprintf(o, " long len; long cap; } Soa%d;\n", i);
        fprintf(o, "static long Soa%d_bound(Soa%d *s, long i) { if (i < 0 || i >= s->len) { "
                   "fprintf(stderr, \"tycho: index %%ld out of bounds (len %%ld)\\n\", i, s->len); exit(1); } return i; }\n", i, i);
    }
    fputs("\n", o);
    /* (3) struct bodies + Option typedefs in containment order (infinite types
     * are rejected here). Enum descriptors above are complete, so a struct/Option
     * may embed an enum by value (it's only 2 words). */
    TBL_RESERVE(g_struct_color, g_nstructs,  g_struct_color_cap);
    TBL_RESERVE(g_opt_color,    g_nopttypes, g_opt_color_cap);
    TBL_RESERVE(g_res_color,    g_nrestypes, g_res_color_cap);
    TBL_RESERVE(g_tup_color,    g_ntuptypes, g_tup_color_cap);
    for (int i = 0; i < g_nstructs; i++)  g_struct_color[i] = 0;
    for (int i = 0; i < g_nopttypes; i++) g_opt_color[i] = 0;
    for (int i = 0; i < g_nrestypes; i++) g_res_color[i] = 0;
    for (int i = 0; i < g_ntuptypes; i++) g_tup_color[i] = 0;
    g_emit_line = 0;
    for (int i = 0; i < g_nstructs; i++)  emit_aggregate(o, STRUCT_TYPE(i));
    for (int i = 0; i < g_nopttypes; i++) emit_aggregate(o, T_OPT_BASE + i);
    for (int i = 0; i < g_nrestypes; i++) emit_aggregate(o, T_RES_BASE + i);
    for (int i = 0; i < g_ntuptypes; i++) emit_aggregate(o, T_TUP_BASE + i);
    /* (3b) enum payload structs: one per variant-with-payload, holding its
     * fields by value (structs/options/arrays/enum-descriptors all emitted
     * above). The payload is heap-allocated, so recursive enums stay finite. */
    for (int i = 0; i < g_nenums; i++) {
        if (g_enums[i].generic) continue;   /* generics: payload structs only for concrete instances */
        for (int v = 0; v < g_enums[i].nvariants; v++) {
            Variant *var = &g_enums[i].variants[v];
            if (var->npayload == 0) continue;
            fprintf(o, "typedef struct {");
            for (int f = 0; f < var->npayload; f++)
                fprintf(o, " %sf%d;", c_type(var->payload[f]), f);
            fprintf(o, " } E_%s_%s;\n", g_enums[i].name, var->name);
        }
    }
    /* (3c) enum cell bodies: a value is a pointer to { int tag; union of the
     * payload-bearing variants' field structs }. One allocation per node, fields
     * inline (no separate payload alloc, no void* indirection). Nullary variants
     * carry no fields and share a static singleton cell — zero per-node alloc and
     * dispatch stays a uniform v->tag. */
    for (int i = 0; i < g_nenums; i++) {
        if (g_enums[i].generic) continue;   /* generics: cell body only for concrete instances */
        EnumDef *ed = &g_enums[i];
        int has_payload = 0;
        for (int v = 0; v < ed->nvariants; v++) if (ed->variants[v].npayload) has_payload = 1;
        fprintf(o, "struct E_%s { int tag;", ed->name);
        if (has_payload) {
            fprintf(o, " union {");
            for (int v = 0; v < ed->nvariants; v++)
                if (ed->variants[v].npayload)
                    fprintf(o, " E_%s_%s %s;", ed->name, ed->variants[v].name, ed->variants[v].name);
            fprintf(o, " } u;");
        }
        fprintf(o, " };\n");
        for (int v = 0; v < ed->nvariants; v++)         /* shared singleton per nullary variant */
            if (ed->variants[v].npayload == 0)
                fprintf(o, "static E_%s _sing_%s_%d = { %d };\n", ed->name, ed->name, v, v);
        for (int mi = 0; mi < g_nmaptypes; mi++)        /* this (fieldless) enum keys a map: the
            * mapc stores TAGS; keys() rebuilds the wrapped values from this table */
            if (g_maptypes[mi].key == ENUM_TYPE(i)) {
                fprintf(o, "static E_%s *const _sing_tab_%s[] = {", ed->name, ed->name);
                for (int v = 0; v < ed->nvariants; v++)
                    fprintf(o, "%s&_sing_%s_%d", v ? ", " : " ", ed->name, v);
                fprintf(o, " };\n");
                break;
            }
    }
    fputs("\n", o);
    /* (3d) function-value typedefs were emitted early (2a') so containers can embed them. */
    for (int i = 0; i < g_nstructs; i++) {          /* (4) copy/eq prototypes */
        if (g_structs[i].generic) continue;   /* generics: no helpers for a template */
        const char *nm = g_structs[i].name;
        if (type_is_heap(STRUCT_TYPE(i)))
            fprintf(o, "static S_%s tycho_copy_S_%s(Arena *a, S_%s v);\n", nm, nm, nm);
        fprintf(o, "static int tycho_eq_S_%s(S_%s a, S_%s b);\n", nm, nm, nm);
        if (struct_keyused(STRUCT_TYPE(i)))   /* composite map key: deep hash (paired with tycho_eq_S_) */
            fprintf(o, "static unsigned long tycho_hash_S_%s(S_%s v);\n", nm, nm);
    }
    for (int i = 0; i < g_ntuptypes; i++)           /* (4) tuple deep-hash prototypes (composite map keys; emitted before bodies so struct/tuple hashes can reference each other) */
        if (struct_keyused(T_TUP_BASE + i))
            fprintf(o, "static unsigned long tycho_hash_T%d(TychoTup%d v);\n", i, i);
    for (int i = 0; i < g_nopttypes; i++)           /* (4) Option-copy prototypes */
        if (type_is_heap(g_opttypes[i].inner) && !has_typaram(T_OPT_BASE + i))
            fprintf(o, "static TychoOpt%d tycho_opt%d_copy(Arena *a, TychoOpt%d v);\n", i, i, i);
    for (int i = 0; i < g_nrestypes; i++)           /* (4) Result-copy prototypes */
        if (type_is_heap(T_RES_BASE + i) && !has_typaram(T_RES_BASE + i))
            fprintf(o, "static TychoRes%d tycho_res%d_copy(Arena *a, TychoRes%d v);\n", i, i, i);
    for (int i = 0; i < g_ntuptypes; i++)           /* (4) tuple-copy prototypes */
        if (type_is_heap(T_TUP_BASE + i) && !has_typaram(T_TUP_BASE + i))
            fprintf(o, "static TychoTup%d tycho_tup%d_copy(Arena *a, TychoTup%d v);\n", i, i, i);
    for (int i = 0; i < g_nenums; i++) {            /* (4) enum copy/eq prototypes */
        if (g_enums[i].generic) continue;   /* generics: no helpers for a template */
        const char *en = g_enums[i].name;
        if (type_is_heap(ENUM_TYPE(i)))
            fprintf(o, "static E_%s *tycho_copy_E_%s(Arena *a, E_%s *v);\n", en, en, en);
        fprintf(o, "static int tycho_eq_E_%s(E_%s *a, E_%s *b);\n", en, en, en);
    }
    for (int i = 0; i < g_nsoatypes; i++) {         /* (4) soa op prototypes (bodies are late) */
        const char *sn = g_structs[STRUCT_ID(g_soatypes[i].st)].name;
        fprintf(o, "static void Soa%d_push(Arena*, Soa%d*, S_%s);\n", i, i, sn);
        fprintf(o, "static Soa%d Soa%d_copy(Arena*, Soa%d);\n", i, i, i);
        fprintf(o, "static int Soa%d_eq(Soa%d, Soa%d);\n", i, i, i);
        fprintf(o, "static S_%s Soa%d_pop(Soa%d *);\n", sn, i, i);
    }
    for (int i = 0; i < g_narrtypes; i++) {         /* (4) array-op prototypes */
        if (has_typaram(T_ARRC_BASE + i)) continue;   /* generics: `[$T]` from a template -- transient */
        const char *ct = c_type(g_arrtypes[i].elem);
        fprintf(o, "static TychoArrC%d tycho_arr_C%d_with_cap(Arena*, long);\n", i, i);
        fprintf(o, "static void tycho_arr_C%d_push(Arena*, TychoArrC%d*, %s);\n", i, i, ct);
        fprintf(o, "static void tycho_arr_C%d_reserve(Arena*, TychoArrC%d*, long);\n", i, i);
        fprintf(o, "static void tycho_arr_C%d_grow(Arena*, %s**, long*, long);\n", i, ct);
        fprintf(o, "static %stycho_arr_C%d_pop(Arena*, TychoArrC%d*);\n", ct, i, i);
        fprintf(o, "static %stycho_arr_C%d_get(TychoArrC%d, long);\n", ct, i, i);
        fprintf(o, "static %s*tycho_arr_C%d_ptr(TychoArrC%d*, long);\n", ct, i, i);
        fprintf(o, "static void tycho_arr_C%d_set(Arena*, TychoArrC%d*, long, %s);\n", i, i, ct);
        fprintf(o, "static TychoArrC%d tycho_arr_C%d_copy(Arena*, TychoArrC%d);\n", i, i, i);
        fprintf(o, "static int tycho_arr_C%d_eq(TychoArrC%d, TychoArrC%d);\n", i, i, i);
        if (struct_keyused(T_ARRC_BASE + i))   /* composite map key: deep hash */
            fprintf(o, "static unsigned long tycho_arr_C%d_hash(TychoArrC%d);\n", i, i);
    }
    for (int i = 0; i < g_nmaptypes; i++) {         /* (4) composite-map copy/eq prototypes: a struct/array/tuple FIELD of composite-map type calls these in its copy/eq body, which is emitted before the map family itself (7a') -- without the proto the struct copier sees an implicit declaration */
        if (has_typaram(T_MAPC_BASE + i)) continue;   /* generics: a `[$K: $V]` template map -- transient, never emitted */
        fprintf(o, "static TychoMapC%d tycho_mapc%d_copy(Arena*, TychoMapC%d);\n", i, i, i);
        fprintf(o, "static int tycho_mapc%d_eq(TychoMapC%d, TychoMapC%d);\n", i, i, i);
    }
    fputs("\n", o);
    /* (5) deep-copy body per heap-bearing struct: re-home every heap field into
     * arena `a`. Non-heap fields are copied by the initial `r = v`. */
    for (int i = 0; i < g_nstructs; i++) {
        if (g_structs[i].generic) continue;   /* generics: a template has no concrete fields to copy */
        StructDef *sd = &g_structs[i];
        if (!type_is_heap(STRUCT_TYPE(i))) continue;
        fprintf(o, "static S_%s tycho_copy_S_%s(Arena *a, S_%s v) {\n", sd->name, sd->name, sd->name);
        fprintf(o, "    S_%s r = v;\n", sd->name);
        for (int j = 0; j < sd->nfields; j++) {
            Type ft = sd->fields[j].type;
            if (!type_is_heap(ft)) continue;
            char *src = sfmt("v.f_%s", sd->fields[j].name);
            fprintf(o, "    r.f_%s = %s;\n", sd->fields[j].name, copy_into(ft, "a", src));
        }
        fprintf(o, "    return r;\n}\n\n");
    }
    /* (6) structural-equality body per struct, field-wise. */
    for (int i = 0; i < g_nstructs; i++) {
        if (g_structs[i].generic) continue;   /* generics: a template has no concrete fields to compare */
        StructDef *sd = &g_structs[i];
        fprintf(o, "static int tycho_eq_S_%s(S_%s a, S_%s b) {\n", sd->name, sd->name, sd->name);
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
    /* (6b) deep-hash body per hashable struct (composite map keys). Folds field
     * hashes FNV-style, seeded from tycho_hash_k0 so the per-process seed defends
     * against hash-flooding and equal-by-== values always hash equal. Field order
     * matters (the multiply runs before the xor). */
    for (int i = 0; i < g_nstructs; i++) {
        if (g_structs[i].generic) continue;
        if (!struct_keyused(STRUCT_TYPE(i))) continue;
        StructDef *sd = &g_structs[i];
        fprintf(o, "static unsigned long tycho_hash_S_%s(S_%s v) {\n", sd->name, sd->name);
        fprintf(o, "    unsigned long h = tycho_hash_k0;\n");
        for (int j = 0; j < sd->nfields; j++) {
            char *vf = sfmt("v.f_%s", sd->fields[j].name);
            fprintf(o, "    h = h * 1099511628211UL ^ %s;\n", gen_hash(sd->fields[j].type, vf));
        }
        fprintf(o, "    return h;\n}\n\n");
    }
    /* (6c) deep-hash body per tuple used as a (nested) composite map key. Tuple == is
     * inline in gen_eq, but the hash is a function (a stateful FNV fold); element
     * access is ._0/._1. Same seeded fold as the struct hash. */
    for (int i = 0; i < g_ntuptypes; i++) {
        if (!struct_keyused(T_TUP_BASE + i)) continue;
        fprintf(o, "static unsigned long tycho_hash_T%d(TychoTup%d v) {\n", i, i);
        fprintf(o, "    unsigned long h = tycho_hash_k0;\n");
        for (int j = 0; j < tup_n(T_TUP_BASE + i); j++) {
            char *vf = sfmt("v._%d", j);
            fprintf(o, "    h = h * 1099511628211UL ^ %s;\n", gen_hash(tup_elem(T_TUP_BASE + i, j), vf));
        }
        fprintf(o, "    return h;\n}\n\n");
    }
    /* (7) composite-array op bodies (typedef already emitted in step 2). Each
     * deep-copies its elements through the same seam as the [string] array. */
    for (int i = 0; i < g_narrtypes; i++) {
        if (has_typaram(T_ARRC_BASE + i)) continue;   /* generics: `[$T]` from a template -- transient */
        Type et = g_arrtypes[i].elem;
        const char *ct = c_type(et);              /* element C type (trailing space) */
        fprintf(o,
            "static TychoArrC%d tycho_arr_C%d_with_cap(Arena *a, long cap) {\n"
            "    TychoArrC%d r; r.len = 0; r.cap = cap;\n"
            "    r.data = cap > 0 ? (%s*)arena_alloc(a, (size_t)cap * sizeof(%s)) : 0;\n"
            "    return r;\n}\n", i, i, i, ct, ct);
        fprintf(o,
            "static void tycho_arr_C%d_push(Arena *a, TychoArrC%d *xs, %sv) {\n"
            "    if (xs->len == xs->cap) {\n"
            "        long nc = xs->cap ? xs->cap * 2 : 4;\n"
            "        %s*nd = (%s*)arena_alloc(a, (size_t)nc * sizeof(%s));\n"
            "        for (long i = 0; i < xs->len; i++) nd[i] = xs->data[i];\n"
            "        if (xs->cap) arena_recycle(a, xs->data, (size_t)xs->cap * sizeof(%s));\n"  /* dead spine; element heap lives on via nd */
            "        xs->data = nd; xs->cap = nc;\n    }\n"
            "    xs->data[xs->len++] = %s;\n}\n",
            i, i, ct, ct, ct, ct, ct, copy_into(et, "a", "v"));
        fprintf(o,   /* capacity hint (reserve): grow the spine to n if larger; elements' heap lives on via nd (shallow spine copy, like push's regrow) */
            "static void tycho_arr_C%d_reserve(Arena *a, TychoArrC%d *xs, long n) {\n"
            "    if (n <= xs->cap) return;\n"
            "    %s*nd = (%s*)arena_alloc(a, (size_t)n * sizeof(%s));\n"
            "    for (long i = 0; i < xs->len; i++) nd[i] = xs->data[i];\n"
            "    if (xs->cap) arena_recycle(a, xs->data, (size_t)xs->cap * sizeof(%s));\n"
            "    xs->data = nd; xs->cap = n;\n}\n", i, i, ct, ct, ct, ct);
        fprintf(o,   /* push-loop fusion grow hook: regrow the spine (shallow copy); elements were already deep-copied into `a` at each fused store */
            "static void tycho_arr_C%d_grow(Arena *a, %s**data, long *cap, long len) {\n"
            "    long nc = *cap ? *cap * 2 : 4;\n"
            "    %s*nd = (%s*)arena_alloc(a, (size_t)nc * sizeof(%s));\n"
            "    for (long i = 0; i < len; i++) nd[i] = (*data)[i];\n"
            "    if (*cap) arena_recycle(a, *data, (size_t)*cap * sizeof(%s));\n"
            "    *data = nd; *cap = nc;\n}\n",
            i, ct, ct, ct, ct, ct);
        fprintf(o,   /* pop: shrink + return the last element, deep-copied into `a` */
            "static %stycho_arr_C%d_pop(Arena *a, TychoArrC%d *xs) {\n"
            "    if (xs->len == 0) { fprintf(stderr, \"tycho: pop from an empty array\\n\"); exit(1); }\n"
            "    xs->len--;\n    return %s;\n}\n",
            ct, i, i, copy_into(et, "a", "xs->data[xs->len]"));
        fprintf(o,
            "static %stycho_arr_C%d_get(TychoArrC%d xs, long i) {\n"
            "    if (i < 0 || i >= xs.len) { fprintf(stderr, \"tycho: index %%ld out of bounds (len %%ld)\\n\", i, xs.len); exit(1); }\n"
            "    return xs.data[i];\n}\n", ct, i, i);
        fprintf(o,   /* projection: a bounds-checked pointer into the buffer, so an */
            "static %s*tycho_arr_C%d_ptr(TychoArrC%d *xs, long i) {\n"   /* element is a mutable lvalue */
            "    if (i < 0 || i >= xs->len) { fprintf(stderr, \"tycho: index %%ld out of bounds (len %%ld)\\n\", i, xs->len); exit(1); }\n"
            "    return &xs->data[i];\n}\n", ct, i, i);
        fprintf(o,
            "static void tycho_arr_C%d_set(Arena *a, TychoArrC%d *xs, long i, %sv) {\n"
            "    if (i < 0 || i >= xs->len) { fprintf(stderr, \"tycho: index %%ld out of bounds (len %%ld)\\n\", i, xs->len); exit(1); }\n"
            "    xs->data[i] = %s;\n}\n", i, i, ct, copy_into(et, "a", "v"));
        fprintf(o,
            "static TychoArrC%d tycho_arr_C%d_copy(Arena *a, TychoArrC%d src) {\n"
            "    TychoArrC%d r = tycho_arr_C%d_with_cap(a, src.len); r.len = src.len;\n"
            "    for (long i = 0; i < src.len; i++) r.data[i] = %s;\n"
            "    return r;\n}\n", i, i, i, i, i, copy_into(et, "a", "src.data[i]"));
        fprintf(o,
            "static int tycho_arr_C%d_eq(TychoArrC%d x, TychoArrC%d y) {\n"
            "    if (x.len != y.len) return 0;\n"
            "    for (long i = 0; i < x.len; i++) if (!(%s)) return 0;\n"
            "    return 1;\n}\n\n", i, i, i, gen_eq(et, "x.data[i]", "y.data[i]"));
        if (struct_keyused(T_ARRC_BASE + i))   /* composite-element array used as a (nested) map key: order-sensitive deep hash */
            fprintf(o,
                "static unsigned long tycho_arr_C%d_hash(TychoArrC%d x) {\n"
                "    unsigned long h = tycho_hash_k0;\n"
                "    for (long i = 0; i < x.len; i++) h = h * 1099511628211UL ^ %s;\n"
                "    return h;\n}\n\n", i, i, gen_hash(et, "x.data[i]"));
    }
    /* (7a') composite-map ops [string: V] — a parameterized copy of the embedded
     * TychoMapSI (open addressing, NULL-empty slots + backward-shift delete, keyed
     * SipHash string keys), with the VALUE
     * generalized to any type: put deep-copies the value into the map's arena via
     * copy_into, exactly like a composite-array element. Emitted after struct/array
     * bodies so a struct/array value type's copy fn is already available. */
    for (int i = 0; i < g_nmaptypes; i++) {
        if (has_typaram(T_MAPC_BASE + i)) continue;   /* generics: a `[$K: $V]` template map -- transient, never emitted */
        const char *ct = c_type(g_maptypes[i].val);
        char *vcopy = copy_into(g_maptypes[i].val, "a", "v");   /* deep-copy the value into arena a */
        if (mapkey_composite(g_maptypes[i].key)) {   /* struct keys: occupancy scheme like int keys, but K stored by value, deep-hashed (tycho_hash_S_*) and deep-compared (tycho_eq_S_*); key deep-copied into the map arena on put */
            Type keyt = g_maptypes[i].key;
            const char *kt = c_type(keyt);
            Type kat = arr_of(keyt);            /* keys() returns [K] */
            fprintf(o,
                "static TychoMapC%d tycho_mapc%d_with_cap(Arena *a, long cap) {\n"
                "    TychoMapC%d m; long c = 4; while (c < cap) c *= 2; if (cap == 0) c = 0;\n"
                "    m.cap = c; m.len = 0; m.used = 0; m.nxt = 0; m.prv = 0; m.head = -1; m.tail = -1; if (c == 0) { m.keys = 0; m.vals = 0; m.occ = 0; return m; }\n"
                "    m.keys = (%s*)arena_alloc(a, (size_t)c * sizeof(%s));\n"
                "    m.vals = (%s*)arena_alloc(a, (size_t)c * sizeof(%s));\n"
                "    m.occ = (unsigned char *)arena_alloc(a, (size_t)c);\n"
                "    m.nxt = (long *)arena_alloc(a, (size_t)c * sizeof(long));\n"
                "    m.prv = (long *)arena_alloc(a, (size_t)c * sizeof(long));\n"
                "    for (long i = 0; i < c; i++) m.occ[i] = 0; return m;\n}\n", i, i, i, kt, kt, ct, ct);
            fprintf(o,
                "static long tycho_mapc%d_find(TychoMapC%d m, %sk) {\n"
                "    if (m.cap == 0) return -1; unsigned long mask = (unsigned long)m.cap - 1; long i = (long)(%s & mask);\n"
                "    while (m.occ[i] != 0) { if (m.occ[i] == 1 && %s) return i; i = (long)((i + 1) & mask); }\n"
                "    return -1;\n}\n", i, i, kt, gen_hash(keyt, "k"), gen_eq(keyt, "m.keys[i]", "k"));
            fprintf(o,
                "static long tycho_mapc%d_slot(TychoMapC%d m, %sk) {\n"
                "    unsigned long mask = (unsigned long)m.cap - 1; long i = (long)(%s & mask), tomb = -1;\n"
                "    while (m.occ[i] != 0) { if (m.occ[i] == 2) { if (tomb < 0) tomb = i; } else if (%s) return i; i = (long)((i + 1) & mask); }\n"
                "    return tomb >= 0 ? tomb : i;\n}\n", i, i, kt, gen_hash(keyt, "k"), gen_eq(keyt, "m.keys[i]", "k"));
            fprintf(o,
                "static void tycho_mapc%d_put(Arena *a, TychoMapC%d *m, %sk, %sv) {\n"
                "    if (m->cap == 0 || (m->used + 1) * 2 > m->cap) {\n"
                "        long nc = ((m->len + 1) * 2 > m->cap) ? (m->cap ? m->cap * 2 : 4) : (m->cap ? m->cap : 4);\n"
                "        TychoMapC%d n = tycho_mapc%d_with_cap(a, nc);\n"
                "        for (long o = m->cap ? m->head : -1; o >= 0; o = m->nxt[o]) { long s = tycho_mapc%d_slot(n, m->keys[o]); n.keys[s] = m->keys[o]; n.vals[s] = m->vals[o]; n.occ[s] = 1; n.len++; n.used++; tycho_ord_link(n.nxt, n.prv, &n.head, &n.tail, s); }\n"
                "        *m = n;\n    }\n"
                "    long s = tycho_mapc%d_slot(*m, k);\n"
                "    if (m->occ[s] != 1) { if (m->occ[s] == 0) m->used++; m->occ[s] = 1; m->keys[s] = %s; m->len++; tycho_ord_link(m->nxt, m->prv, &m->head, &m->tail, s); }\n"
                "    m->vals[s] = %s;\n}\n", i, i, kt, ct, i, i, i, i, copy_into(keyt, "a", "k"), vcopy);
            fprintf(o,
                "static void tycho_mapc%d_del(TychoMapC%d *m, %sk) {\n"
                "    long s = tycho_mapc%d_find(*m, k); if (s < 0) return;\n"
                "    tycho_ord_unlink(m->nxt, m->prv, &m->head, &m->tail, s); m->len--; m->used--;\n"
                "    unsigned long mask = (unsigned long)m->cap - 1; long i = s, j = s;\n"
                "    for (;;) { m->occ[i] = 0;\n"
                "        for (;;) { j = (long)((j + 1) & mask); if (m->occ[j] == 0) return;\n"
                "            long h = (long)(%s & mask);\n"
                "            if (i <= j) { if (i < h && h <= j) continue; } else { if (i < h || h <= j) continue; } break; }\n"
                "        m->keys[i] = m->keys[j]; m->vals[i] = m->vals[j]; m->occ[i] = 1;\n"
                "        long pp = m->prv[j], nn = m->nxt[j]; m->nxt[i] = nn; m->prv[i] = pp;\n"
                "        if (pp >= 0) m->nxt[pp] = i; else m->head = i; if (nn >= 0) m->prv[nn] = i; else m->tail = i; i = j; }\n}\n",
                i, i, kt, i, gen_hash(keyt, "m->keys[j]"));
            fprintf(o,
                "static TychoMapC%d tycho_mapc%d_copy(Arena *a, TychoMapC%d src) {\n"
                "    TychoMapC%d r = tycho_mapc%d_with_cap(a, src.len ? src.len * 2 : 0);\n"
                "    for (long s = src.cap ? src.head : -1; s >= 0; s = src.nxt[s]) tycho_mapc%d_put(a, &r, src.keys[s], src.vals[s]);\n"
                "    return r;\n}\n", i, i, i, i, i, i);
            fprintf(o,
                "static TychoMapC%d tycho_mapc%d_set(Arena *a, TychoMapC%d m, %sk, %sv) {\n"
                "    TychoMapC%d r = tycho_mapc%d_copy(a, m); tycho_mapc%d_put(a, &r, k, v); return r;\n}\n", i, i, i, kt, ct, i, i, i);
            fprintf(o,
                "static TychoMapC%d tycho_mapc%d_del_pure(Arena *a, TychoMapC%d m, %sk) {\n"
                "    TychoMapC%d r = tycho_mapc%d_copy(a, m); tycho_mapc%d_del(&r, k); return r;\n}\n", i, i, i, kt, i, i, i);
            fprintf(o,
                "static %stycho_mapc%d_get(TychoMapC%d m, %sk, %sdflt) {\n"
                "    long s = tycho_mapc%d_find(m, k); return s < 0 ? dflt : m.vals[s];\n}\n", ct, i, i, kt, ct, i);
            fprintf(o,
                "static int tycho_mapc%d_has(TychoMapC%d m, %sk) { return tycho_mapc%d_find(m, k) >= 0; }\n", i, i, kt, i);
            fprintf(o,
                "static %stycho_mapc%d_keys(Arena *a, TychoMapC%d m) {\n"
                "    %sr = tycho_arr_%s_with_cap(a, m.len);\n"
                "    for (long s = m.cap ? m.head : -1; s >= 0; s = m.nxt[s]) tycho_arr_%s_push(a, &r, m.keys[s]);\n"
                "    return r;\n}\n", c_type(kat), i, i, c_type(kat), arr_fn(kat), arr_fn(kat));
            fprintf(o,
                "static int tycho_mapc%d_eq(TychoMapC%d x, TychoMapC%d y) {\n"
                "    if (x.len != y.len) return 0;\n"
                "    for (long i = 0; i < x.cap; i++) if (x.occ[i] == 1) { long s = tycho_mapc%d_find(y, x.keys[i]); if (s < 0 || !(%s)) return 0; }\n"
                "    return 1;\n}\n\n", i, i, i, i, gen_eq(g_maptypes[i].val, "y.vals[s]", "x.vals[i]"));
            fprintf(o,   /* in-place value projection: find-or-insert, return &slot value */
                "static inline %s*tycho_mapc%d_slotptr(Arena *a, TychoMapC%d *m, %sk) {\n"
                "    if (m->cap == 0 || (m->used + 1) * 2 > m->cap) {\n"
                "        long nc = ((m->len + 1) * 2 > m->cap) ? (m->cap ? m->cap * 2 : 4) : (m->cap ? m->cap : 4);\n"
                "        TychoMapC%d n = tycho_mapc%d_with_cap(a, nc);\n"
                "        for (long o = m->cap ? m->head : -1; o >= 0; o = m->nxt[o]) { long s = tycho_mapc%d_slot(n, m->keys[o]); n.keys[s] = m->keys[o]; n.vals[s] = m->vals[o]; n.occ[s] = 1; n.len++; n.used++; tycho_ord_link(n.nxt, n.prv, &n.head, &n.tail, s); }\n"
                "        *m = n;\n    }\n"
                "    long s = tycho_mapc%d_slot(*m, k);\n"
                "    if (m->occ[s] != 1) { if (m->occ[s] == 0) m->used++; m->occ[s] = 1; m->keys[s] = %s; m->len++; tycho_ord_link(m->nxt, m->prv, &m->head, &m->tail, s); m->vals[s] = (%s){0}; }\n"
                "    return &m->vals[s];\n}\n\n", ct, i, i, kt, i, i, i, i, copy_into(keyt, "a", "k"), ct);
            continue;
        }
        if (mapkey_intrep(g_maptypes[i].key)) {   /* int(-rep) keys incl. enum tags: occupancy-array scheme (mirror TychoMapII; 0 is a real key) */
            fprintf(o,
                "static TychoMapC%d tycho_mapc%d_with_cap(Arena *a, long cap) {\n"
                "    TychoMapC%d m; long c = 4; while (c < cap) c *= 2; if (cap == 0) c = 0;\n"
                "    m.cap = c; m.len = 0; m.used = 0; m.nxt = 0; m.prv = 0; m.head = -1; m.tail = -1; if (c == 0) { m.keys = 0; m.vals = 0; m.occ = 0; return m; }\n"
                "    m.keys = (long *)arena_alloc(a, (size_t)c * sizeof(long));\n"
                "    m.vals = (%s*)arena_alloc(a, (size_t)c * sizeof(%s));\n"
                "    m.occ = (unsigned char *)arena_alloc(a, (size_t)c);\n"
                "    m.nxt = (long *)arena_alloc(a, (size_t)c * sizeof(long));\n"
                "    m.prv = (long *)arena_alloc(a, (size_t)c * sizeof(long));\n"
                "    for (long i = 0; i < c; i++) m.occ[i] = 0; return m;\n}\n", i, i, i, ct, ct);
            fprintf(o,
                "static long tycho_mapc%d_find(TychoMapC%d m, long k) {\n"
                "    if (m.cap == 0) return -1; unsigned long mask = (unsigned long)m.cap - 1; long i = (long)(tycho_ik_hash(k) & mask);\n"
                "    while (m.occ[i] != 0) { if (m.occ[i] == 1 && m.keys[i] == k) return i; i = (long)((i + 1) & mask); }\n"
                "    return -1;\n}\n", i, i);
            fprintf(o,
                "static long tycho_mapc%d_slot(TychoMapC%d m, long k) {\n"
                "    unsigned long mask = (unsigned long)m.cap - 1; long i = (long)(tycho_ik_hash(k) & mask), tomb = -1;\n"
                "    while (m.occ[i] != 0) { if (m.occ[i] == 2) { if (tomb < 0) tomb = i; } else if (m.keys[i] == k) return i; i = (long)((i + 1) & mask); }\n"
                "    return tomb >= 0 ? tomb : i;\n}\n", i, i);
            fprintf(o,
                "static void tycho_mapc%d_put(Arena *a, TychoMapC%d *m, long k, %sv) {\n"
                "    if (m->cap == 0 || (m->used + 1) * 2 > m->cap) {\n"
                "        long nc = ((m->len + 1) * 2 > m->cap) ? (m->cap ? m->cap * 2 : 4) : (m->cap ? m->cap : 4);\n"
                "        TychoMapC%d n = tycho_mapc%d_with_cap(a, nc);\n"
                "        for (long o = m->cap ? m->head : -1; o >= 0; o = m->nxt[o]) { long s = tycho_mapc%d_slot(n, m->keys[o]); n.keys[s] = m->keys[o]; n.vals[s] = m->vals[o]; n.occ[s] = 1; n.len++; n.used++; tycho_ord_link(n.nxt, n.prv, &n.head, &n.tail, s); }\n"
                "        *m = n;\n    }\n"
                "    long s = tycho_mapc%d_slot(*m, k);\n"
                "    if (m->occ[s] != 1) { if (m->occ[s] == 0) m->used++; m->occ[s] = 1; m->keys[s] = k; m->len++; tycho_ord_link(m->nxt, m->prv, &m->head, &m->tail, s); }\n"
                "    m->vals[s] = %s;\n}\n", i, i, ct, i, i, i, i, vcopy);
            fprintf(o,
                "static void tycho_mapc%d_del(TychoMapC%d *m, long k) {\n"
                "    long s = tycho_mapc%d_find(*m, k); if (s < 0) return;\n"
                "    tycho_ord_unlink(m->nxt, m->prv, &m->head, &m->tail, s); m->len--; m->used--;\n"
                "    unsigned long mask = (unsigned long)m->cap - 1; long i = s, j = s;\n"
                "    for (;;) { m->occ[i] = 0;\n"
                "        for (;;) { j = (long)((j + 1) & mask); if (m->occ[j] == 0) return;\n"
                "            long h = (long)(tycho_ik_hash(m->keys[j]) & mask);\n"
                "            if (i <= j) { if (i < h && h <= j) continue; } else { if (i < h || h <= j) continue; } break; }\n"
                "        m->keys[i] = m->keys[j]; m->vals[i] = m->vals[j]; m->occ[i] = 1;\n"
                "        long pp = m->prv[j], nn = m->nxt[j]; m->nxt[i] = nn; m->prv[i] = pp;\n"
                "        if (pp >= 0) m->nxt[pp] = i; else m->head = i; if (nn >= 0) m->prv[nn] = i; else m->tail = i; i = j; }\n}\n", i, i, i);
            fprintf(o,
                "static TychoMapC%d tycho_mapc%d_copy(Arena *a, TychoMapC%d src) {\n"
                "    TychoMapC%d r = tycho_mapc%d_with_cap(a, src.len ? src.len * 2 : 0);\n"
                "    for (long s = src.cap ? src.head : -1; s >= 0; s = src.nxt[s]) tycho_mapc%d_put(a, &r, src.keys[s], src.vals[s]);\n"
                "    return r;\n}\n", i, i, i, i, i, i);
            fprintf(o,
                "static TychoMapC%d tycho_mapc%d_set(Arena *a, TychoMapC%d m, long k, %sv) {\n"
                "    TychoMapC%d r = tycho_mapc%d_copy(a, m); tycho_mapc%d_put(a, &r, k, v); return r;\n}\n", i, i, i, ct, i, i, i);
            fprintf(o,
                "static TychoMapC%d tycho_mapc%d_del_pure(Arena *a, TychoMapC%d m, long k) {\n"
                "    TychoMapC%d r = tycho_mapc%d_copy(a, m); tycho_mapc%d_del(&r, k); return r;\n}\n", i, i, i, i, i, i);
            fprintf(o,
                "static %stycho_mapc%d_get(TychoMapC%d m, long k, %sdflt) {\n"
                "    long s = tycho_mapc%d_find(m, k); return s < 0 ? dflt : m.vals[s];\n}\n", ct, i, i, ct, i);
            fprintf(o,
                "static int tycho_mapc%d_has(TychoMapC%d m, long k) { return tycho_mapc%d_find(m, k) >= 0; }\n", i, i, i);
            {   /* keys() returns [K] — TychoArrInt, TychoArrC<n> for a newtype, or [E] for a
                 * fieldless-enum key, rebuilt from the singleton table (tags are stored) */
                Type kt = g_maptypes[i].key;
                Type kat = arr_of(kt);
                char *kv = IS_ENUM(kt) ? sfmt("_sing_tab_%s[m.keys[s]]", g_enums[ENUM_ID(kt)].name)
                                       : sfmt("m.keys[s]");
                fprintf(o,
                    "static %stycho_mapc%d_keys(Arena *a, TychoMapC%d m) {\n"
                    "    %sr = tycho_arr_%s_with_cap(a, m.len);\n"
                    "    for (long s = m.cap ? m.head : -1; s >= 0; s = m.nxt[s]) tycho_arr_%s_push(a, &r, %s);\n"
                    "    return r;\n}\n", c_type(kat), i, i, c_type(kat), arr_fn(kat), arr_fn(kat), kv);
            }
            fprintf(o,
                "static int tycho_mapc%d_eq(TychoMapC%d x, TychoMapC%d y) {\n"
                "    if (x.len != y.len) return 0;\n"
                "    for (long i = 0; i < x.cap; i++) if (x.occ[i] == 1) { long s = tycho_mapc%d_find(y, x.keys[i]); if (s < 0 || !(%s)) return 0; }\n"
                "    return 1;\n}\n\n", i, i, i, i, gen_eq(g_maptypes[i].val, "y.vals[s]", "x.vals[i]"));
            fprintf(o,   /* in-place value projection: find-or-insert, return &slot value (#2) */
                "static inline %s*tycho_mapc%d_slotptr(Arena *a, TychoMapC%d *m, long k) {\n"
                "    if (m->cap == 0 || (m->used + 1) * 2 > m->cap) {\n"
                "        long nc = ((m->len + 1) * 2 > m->cap) ? (m->cap ? m->cap * 2 : 4) : (m->cap ? m->cap : 4);\n"
                "        TychoMapC%d n = tycho_mapc%d_with_cap(a, nc);\n"
                "        for (long o = m->cap ? m->head : -1; o >= 0; o = m->nxt[o]) { long s = tycho_mapc%d_slot(n, m->keys[o]); n.keys[s] = m->keys[o]; n.vals[s] = m->vals[o]; n.occ[s] = 1; n.len++; n.used++; tycho_ord_link(n.nxt, n.prv, &n.head, &n.tail, s); }\n"
                "        *m = n;\n    }\n"
                "    long s = tycho_mapc%d_slot(*m, k);\n"
                "    if (m->occ[s] != 1) { if (m->occ[s] == 0) m->used++; m->occ[s] = 1; m->keys[s] = k; m->len++; tycho_ord_link(m->nxt, m->prv, &m->head, &m->tail, s); m->vals[s] = (%s){0}; }\n"
                "    return &m->vals[s];\n}\n\n", ct, i, i, i, i, i, i, ct);
            continue;
        }
        fprintf(o,
            "static TychoMapC%d tycho_mapc%d_with_cap(Arena *a, long cap) {\n"
            "    TychoMapC%d m; long c = 4; while (c < cap) c *= 2; if (cap == 0) c = 0;\n"
            "    m.cap = c; m.len = 0; m.used = 0; m.nxt = 0; m.prv = 0; m.head = -1; m.tail = -1; if (c == 0) { m.keys = 0; m.vals = 0; return m; }\n"
            "    m.keys = (char **)arena_alloc(a, (size_t)c * sizeof(char *));\n"
            "    m.vals = (%s*)arena_alloc(a, (size_t)c * sizeof(%s));\n"
            "    m.nxt = (long *)arena_alloc(a, (size_t)c * sizeof(long));\n"
            "    m.prv = (long *)arena_alloc(a, (size_t)c * sizeof(long));\n"
            "    for (long i = 0; i < c; i++) m.keys[i] = 0; return m;\n}\n", i, i, i, ct, ct);
        fprintf(o,
            "static long tycho_mapc%d_find(TychoMapC%d m, const char *k) {\n"
            "    if (m.cap == 0) return -1; unsigned long mask = (unsigned long)m.cap - 1; long i = (long)(tycho_si_hash(k) & mask);\n"
            "    while (m.keys[i] != 0) { if (m.keys[i] != TYCHO_MAP_TOMB && strcmp(m.keys[i], k) == 0) return i; i = (long)((i + 1) & mask); }\n"
            "    return -1;\n}\n", i, i);
        fprintf(o,
            "static long tycho_mapc%d_slot(TychoMapC%d m, const char *k) {\n"
            "    unsigned long mask = (unsigned long)m.cap - 1; long i = (long)(tycho_si_hash(k) & mask); long tomb = -1;\n"
            "    while (m.keys[i] != 0) { if (m.keys[i] == TYCHO_MAP_TOMB) { if (tomb < 0) tomb = i; } else if (strcmp(m.keys[i], k) == 0) return i; i = (long)((i + 1) & mask); }\n"
            "    return tomb >= 0 ? tomb : i;\n}\n", i, i);
        fprintf(o,
            "static void tycho_mapc%d_put(Arena *a, TychoMapC%d *m, const char *k, %sv) {\n"
            "    if (m->cap == 0 || (m->used + 1) * 2 > m->cap) {\n"
            "        long nc = ((m->len + 1) * 2 > m->cap) ? (m->cap ? m->cap * 2 : 4) : (m->cap ? m->cap : 4);\n"
            "        TychoMapC%d n = tycho_mapc%d_with_cap(a, nc);\n"
            "        for (long o = m->cap ? m->head : -1; o >= 0; o = m->nxt[o]) { long s = tycho_mapc%d_slot(n, m->keys[o]); n.keys[s] = m->keys[o]; n.vals[s] = m->vals[o]; n.len++; n.used++; tycho_ord_link(n.nxt, n.prv, &n.head, &n.tail, s); }\n"
            "        *m = n;\n    }\n"
            "    long s = tycho_mapc%d_slot(*m, k);\n"
            "    if (!tycho_map_live(m->keys[s])) { if (m->keys[s] == 0) m->used++; m->keys[s] = tycho_str_copy(a, k); m->len++; tycho_ord_link(m->nxt, m->prv, &m->head, &m->tail, s); }\n"
            "    m->vals[s] = %s;\n}\n", i, i, ct, i, i, i, i, vcopy);
        fprintf(o,
            "static void tycho_mapc%d_del(TychoMapC%d *m, const char *k) {\n"
            "    long s = tycho_mapc%d_find(*m, k); if (s < 0) return;\n"
            "    tycho_ord_unlink(m->nxt, m->prv, &m->head, &m->tail, s); m->len--; m->used--;\n"
            "    unsigned long mask = (unsigned long)m->cap - 1; long i = s, j = s;\n"
            "    for (;;) { m->keys[i] = 0;\n"
            "        for (;;) { j = (long)((j + 1) & mask); if (m->keys[j] == 0) return;\n"
            "            long h = (long)(tycho_si_hash(m->keys[j]) & mask);\n"
            "            if (i <= j) { if (i < h && h <= j) continue; } else { if (i < h || h <= j) continue; } break; }\n"
            "        m->keys[i] = m->keys[j]; m->vals[i] = m->vals[j];\n"
            "        long pp = m->prv[j], nn = m->nxt[j]; m->nxt[i] = nn; m->prv[i] = pp;\n"
            "        if (pp >= 0) m->nxt[pp] = i; else m->head = i; if (nn >= 0) m->prv[nn] = i; else m->tail = i; i = j; }\n}\n", i, i, i);
        fprintf(o,
            "static TychoMapC%d tycho_mapc%d_copy(Arena *a, TychoMapC%d src) {\n"
            "    TychoMapC%d r = tycho_mapc%d_with_cap(a, src.len ? src.len * 2 : 0);\n"
            "    for (long s = src.cap ? src.head : -1; s >= 0; s = src.nxt[s]) tycho_mapc%d_put(a, &r, src.keys[s], src.vals[s]);\n"
            "    return r;\n}\n", i, i, i, i, i, i);
        fprintf(o,
            "static TychoMapC%d tycho_mapc%d_set(Arena *a, TychoMapC%d m, const char *k, %sv) {\n"
            "    TychoMapC%d r = tycho_mapc%d_copy(a, m); tycho_mapc%d_put(a, &r, k, v); return r;\n}\n", i, i, i, ct, i, i, i);
        fprintf(o,
            "static TychoMapC%d tycho_mapc%d_del_pure(Arena *a, TychoMapC%d m, const char *k) {\n"
            "    TychoMapC%d r = tycho_mapc%d_copy(a, m); tycho_mapc%d_del(&r, k); return r;\n}\n", i, i, i, i, i, i);
        fprintf(o,
            "static %stycho_mapc%d_get(TychoMapC%d m, const char *k, %sdflt) {\n"
            "    long s = tycho_mapc%d_find(m, k); return s < 0 ? dflt : m.vals[s];\n}\n", ct, i, i, ct, i);
        fprintf(o,
            "static int tycho_mapc%d_has(TychoMapC%d m, const char *k) { return tycho_mapc%d_find(m, k) >= 0; }\n", i, i, i);
        {   /* keys() returns [K] — TychoArrStr, or TychoArrC<n> when K is a newtype */
            Type kat = arr_of(g_maptypes[i].key);
            fprintf(o,
                "static %stycho_mapc%d_keys(Arena *a, TychoMapC%d m) {\n"
                "    %sr = tycho_arr_%s_with_cap(a, m.len);\n"
                "    for (long s = m.cap ? m.head : -1; s >= 0; s = m.nxt[s]) tycho_arr_%s_push(a, &r, m.keys[s]);\n"
                "    return r;\n}\n", c_type(kat), i, i, c_type(kat), arr_fn(kat), arr_fn(kat));
        }
        fprintf(o,
            "static int tycho_mapc%d_eq(TychoMapC%d x, TychoMapC%d y) {\n"
            "    if (x.len != y.len) return 0;\n"
            "    for (long i = 0; i < x.cap; i++) if (tycho_map_live(x.keys[i])) { long s = tycho_mapc%d_find(y, x.keys[i]); if (s < 0 || !(%s)) return 0; }\n"
            "    return 1;\n}\n\n", i, i, i, i, gen_eq(g_maptypes[i].val, "y.vals[s]", "x.vals[i]"));
        fprintf(o,   /* in-place value projection: find-or-insert, return &slot value (#2) */
            "static inline %s*tycho_mapc%d_slotptr(Arena *a, TychoMapC%d *m, const char *k) {\n"
            "    if (m->cap == 0 || (m->used + 1) * 2 > m->cap) {\n"
            "        long nc = ((m->len + 1) * 2 > m->cap) ? (m->cap ? m->cap * 2 : 4) : (m->cap ? m->cap : 4);\n"
            "        TychoMapC%d n = tycho_mapc%d_with_cap(a, nc);\n"
            "        for (long o = m->cap ? m->head : -1; o >= 0; o = m->nxt[o]) { long s = tycho_mapc%d_slot(n, m->keys[o]); n.keys[s] = m->keys[o]; n.vals[s] = m->vals[o]; n.len++; n.used++; tycho_ord_link(n.nxt, n.prv, &n.head, &n.tail, s); }\n"
            "        *m = n;\n    }\n"
            "    long s = tycho_mapc%d_slot(*m, k);\n"
            "    if (!tycho_map_live(m->keys[s])) { if (m->keys[s] == 0) m->used++; m->keys[s] = tycho_str_copy(a, k); m->len++; tycho_ord_link(m->nxt, m->prv, &m->head, &m->tail, s); m->vals[s] = (%s){0}; }\n"
            "    return &m->vals[s];\n}\n\n", ct, i, i, i, i, i, i, ct);
    }
    /* (7b) SOA types: struct-of-arrays. One growable arena buffer per struct
     * field (named f<idx>) plus a shared len/cap. push grows every buffer in the
     * arena (arenas never realloc — allocate bigger + copy, like the AoS push)
     * and scatters the struct's fields, deep-copying heap fields via copy_into.
     * Emitted after struct bodies (S_<name> is complete) and before fn bodies. */
    for (int i = 0; i < g_nsoatypes; i++) {
        StructDef *sd = &g_structs[STRUCT_ID(g_soatypes[i].st)];
        const char *sn = sd->name;   /* typedef + Soa<id>_bound were emitted early (2c) */
        fprintf(o, "static void Soa%d_push(Arena *a, Soa%d *s, S_%s v) {\n", i, i, sn);
        fprintf(o, "    if (s->len == s->cap) {\n        long nc = s->cap ? s->cap * 2 : 4;\n");
        for (int f = 0; f < sd->nfields; f++) {
            const char *ct = c_type(sd->fields[f].type);
            fprintf(o, "        %s*n%d = (%s*)arena_alloc(a, (size_t)nc * sizeof(%s)); "
                       "for (long i = 0; i < s->len; i++) n%d[i] = s->f%d[i]; s->f%d = n%d;\n",
                    ct, f, ct, ct, f, f, f, f);
        }
        fprintf(o, "        s->cap = nc;\n    }\n");
        for (int f = 0; f < sd->nfields; f++)
            fprintf(o, "    s->f%d[s->len] = %s;\n", f,
                    copy_into(sd->fields[f].type, "a", sfmt("v.f_%s", sd->fields[f].name)));
        fprintf(o, "    s->len++;\n}\n");
        /* pop: shrink len, then gather the (new) last element as a struct value */
        fprintf(o, "static S_%s Soa%d_pop(Soa%d *s) {\n", sn, i, i);
        fprintf(o, "    if (s->len == 0) { fprintf(stderr, \"tycho: pop from an empty array\\n\"); exit(1); }\n");
        fprintf(o, "    s->len--;\n    return (S_%s){", sn);
        for (int f = 0; f < sd->nfields; f++)
            fprintf(o, "%s s->f%d[s->len]", f ? "," : "", f);
        fprintf(o, " };\n}\n");
        /* deep-copy a soa value (value semantics on bind/pass/return): each
         * field buffer is reallocated in the target arena and its elements
         * copied (deep for heap fields via copy_into). */
        fprintf(o, "static Soa%d Soa%d_copy(Arena *a, Soa%d s) {\n", i, i, i);
        fprintf(o, "    Soa%d r; r.len = s.len; r.cap = s.len;\n", i);
        for (int f = 0; f < sd->nfields; f++) {
            const char *ct = c_type(sd->fields[f].type);
            fprintf(o, "    r.f%d = s.len ? (%s*)arena_alloc(a, (size_t)s.len * sizeof(%s)) : 0;\n", f, ct, ct);
            fprintf(o, "    for (long i = 0; i < s.len; i++) r.f%d[i] = %s;\n", f,
                    copy_into(sd->fields[f].type, "a", sfmt("s.f%d[i]", f)));
        }
        fprintf(o, "    return r;\n}\n");
        /* structural equality: same length, then every field equal elementwise */
        fprintf(o, "static int Soa%d_eq(Soa%d a, Soa%d b) {\n", i, i, i);
        fprintf(o, "    if (a.len != b.len) return 0;\n");
        char *conj = NULL;
        for (int f = 0; f < sd->nfields; f++) {
            char *fe = gen_eq(sd->fields[f].type, sfmt("a.f%d[i]", f), sfmt("b.f%d[i]", f));
            conj = conj ? sfmt("%s && %s", conj, fe) : fe;
        }
        fprintf(o, "    for (long i = 0; i < a.len; i++) if (!(%s)) return 0;\n", conj);
        fprintf(o, "    return 1;\n}\n\n");
    }
    /* (8) Option copy bodies (typedefs already emitted in step 3). A copy fn is
     * emitted only for a heap-valued Option; it re-homes the value when present.
     * Recurses via copy_into, which may call a struct copy (above) — both are
     * prototyped, so an Option(Struct) field copies correctly. */
    for (int i = 0; i < g_nopttypes; i++) {
        Type it = g_opttypes[i].inner;
        if (type_is_heap(it) && !has_typaram(T_OPT_BASE + i))
            fprintf(o,
                "static TychoOpt%d tycho_opt%d_copy(Arena *a, TychoOpt%d v) {\n"
                "    if (v.has) v.val = %s;\n"
                "    return v;\n}\n\n", i, i, i, copy_into(it, "a", "v.val"));
    }
    /* (8b) Result copy bodies: re-home only the active variant's value (the
     * inactive field is zero from the designated-initializer construction). */
    for (int i = 0; i < g_nrestypes; i++) {
        if (!type_is_heap(T_RES_BASE + i) || has_typaram(T_RES_BASE + i)) continue;
        Type okt = g_restypes[i].ok, errt = g_restypes[i].err;
        fprintf(o,
            "static TychoRes%d tycho_res%d_copy(Arena *a, TychoRes%d v) {\n"
            "    if (v.ok) v.okv = %s;\n"
            "    else v.errv = %s;\n"
            "    return v;\n}\n\n", i, i, i,
            copy_into(okt, "a", "v.okv"), copy_into(errt, "a", "v.errv"));
    }
    /* (8c) tuple copy bodies: re-home each heap element field. */
    for (int i = 0; i < g_ntuptypes; i++) {
        if (!type_is_heap(T_TUP_BASE + i) || has_typaram(T_TUP_BASE + i)) continue;
        TupType *tt = &g_tuptypes[i];
        fprintf(o, "static TychoTup%d tycho_tup%d_copy(Arena *a, TychoTup%d v) {\n", i, i, i);
        for (int j = 0; j < tt->n; j++)
            if (type_is_heap(tt->elems[j]))
                fprintf(o, "    v._%d = %s;\n", j, copy_into(tt->elems[j], "a", sfmt("v._%d", j)));
        fprintf(o, "    return v;\n}\n\n");
    }
    /* (9) enum copy + eq bodies. copy allocates a fresh payload per tag and
     * deep-copies its fields (so two enum values never share a payload); eq
     * compares the tag, then the active variant's fields. Recurse via
     * copy_into / gen_eq — all prototyped, so recursive enums (ASTs) work. */
    for (int i = 0; i < g_nenums; i++) {
        if (g_enums[i].generic) continue;   /* generics: copy/eq bodies only for concrete instances */
        EnumDef *ed = &g_enums[i];
        const char *en = ed->name;
        if (type_is_heap(ENUM_TYPE(i))) {
            fprintf(o, "static E_%s *tycho_copy_E_%s(Arena *a, E_%s *v) {\n", en, en, en);
            for (int v2 = 0; v2 < ed->nvariants; v2++) {
                Variant *var = &ed->variants[v2];
                if (var->npayload == 0) continue;
                fprintf(o, "    if (v->tag == %d) {\n", v2);
                fprintf(o, "        E_%s *d = (E_%s *)arena_alloc(a, offsetof(E_%s, u) + sizeof(E_%s_%s)); d->tag = %d;\n", en, en, en, en, var->name, v2);
                for (int f = 0; f < var->npayload; f++)
                    fprintf(o, "        d->u.%s.f%d = %s;\n", var->name, f,
                            copy_into(var->payload[f], "a", sfmt("v->u.%s.f%d", var->name, f)));
                fprintf(o, "        return d;\n    }\n");
            }
            fprintf(o, "    return v;\n}\n");   /* nullary variant: shared static singleton, immutable */
        }
        fprintf(o, "static int tycho_eq_E_%s(E_%s *a, E_%s *b) {\n", en, en, en);
        fprintf(o, "    if (a->tag != b->tag) return 0;\n");
        for (int v2 = 0; v2 < ed->nvariants; v2++) {
            Variant *var = &ed->variants[v2];
            if (var->npayload == 0) continue;
            fprintf(o, "    if (a->tag == %d) { return ", v2);
            for (int f = 0; f < var->npayload; f++)
                fprintf(o, "%s%s", gen_eq(var->payload[f], sfmt("a->u.%s.f%d", var->name, f), sfmt("b->u.%s.f%d", var->name, f)),
                        f + 1 < var->npayload ? " && " : "");
            fprintf(o, "; }\n");
        }
        fprintf(o, "    return 1;\n}\n\n");
    }
    for (int i = 0; i < g_nlaminfo; i++) {   /* closure env structs (one per capturing lambda) + its env-copy thunk */
        LamInfo *li = &g_laminfo[i];
        if (li->ftype == T_VOID || li->ncap == 0) continue;
        fprintf(o, "typedef struct {");
        for (int j = 0; j < li->ncap; j++) fprintf(o, " %sc%d;", c_type(li->proc->params[j].type), j);
        fprintf(o, " } Env_%d;\n", i);
        /* re-home the env (and deep-copy its heap captures) into `a` on closure return */
        fprintf(o, "static void *Env_%d_copy(Arena *a, void *_s) { Env_%d *s = (Env_%d *)_s; Env_%d *d = (Env_%d *)arena_alloc(a, sizeof(Env_%d));", i, i, i, i, i, i);
        for (int j = 0; j < li->ncap; j++) {
            Type ct = li->proc->params[j].type;
            fprintf(o, " d->c%d = %s;", j, copy_into(ct, "a", sfmt("s->c%d", j)));
        }
        fprintf(o, " return d; }\n");
    }
    for (int i = 0; i < prog->n; i++) {
        if (prog->v[i]->generic) continue;   /* generics: a template emits no code; its instances do (below) */
        if (prog->v[i]->is_extern) gen_extern_proto(o, prog->v[i]);   /* FFI: real C ABI decl */
        else gen_proto(o, prog->v[i]);
    }
    for (int i = 0; i < g_nlaminfo; i++)   /* lifted lambda prototypes */
        if (g_laminfo[i].ftype != T_VOID) gen_proto(o, g_laminfo[i].proc);
    for (int i = 0; i < g_parprocs.n; i++)   /* lifted parallel-for chunk prototypes (CC-3) */
        gen_proto(o, g_parprocs.v[i]);
    for (int i = 0; i < g_nginsts; i++)      /* generics: one prototype per monomorphic instance (all resolved up front, so nested ones are covered) */
        gen_proto(o, g_inst_procs[i]);
    fputs("\n", o);
    /* spawn sites: one args struct + thread trampoline each. The trampoline
     * runs the call with the task's root arena as _parent (so the return value
     * lands in the root, like any return-to-caller), parks the result in a
     * root-allocated slot, then flushes this thread's block pool (TLS dies
     * with the thread; un-flushed free blocks would leak). */
    for (int i = 0; i < g_nspawn; i++) {
        Sig *s = &g_sigs[g_spawn[i]];
        fprintf(o, "typedef struct { HTask *t;");
        for (int j = 0; j < s->nparams; j++) fprintf(o, " %sa%d;", c_type(s->params[j]), j);
        fprintf(o, " } HSpawnA_%d;\n", i);
        fprintf(o, "static void *tycho_spawn_%d(void *_p) { HSpawnA_%d *_a = (HSpawnA_%d *)_p; "
                   "%s_r = h_%s(&_a->t->root", i, i, i, c_type(s->ret), s->name);
        for (int j = 0; j < s->nparams; j++) fprintf(o, ", _a->a%d", j);
        fprintf(o, "); %s*_s = (%s*)arena_alloc(&_a->t->root, sizeof(%s)); *_s = _r; "
                   "_a->t->ret = _s; tycho_pool_flush(); return 0; }\n",
                c_type(s->ret), c_type(s->ret), c_type(s->ret));
    }
    if (g_nspawn) fputs("\n", o);
    /* channel element-type wrappers (CC-4): the type-aware deep copies in and
     * out of the slot arenas, run while the channel mutex is held (the
     * begin/commit pairs in the runtime bracket them). */
    for (int i = 0; i < g_nchantypes; i++) {
        Type it = g_chantypes[i].inner;
        /* the deep copy runs in the CLAIMED cell -- exclusive between claim
         * and commit, no lock held (CC-5 lock-free fast path) */
        fprintf(o, "static void tycho_chan_send_%d(HChan *_ch, %s_v) { HCell *_c = tycho_chan_send_cell(_ch); "
                   "%s*_p = (%s*)arena_alloc(&_c->arena, sizeof(%s)); *_p = %s; tycho_chan_send_commit(_ch, _c, _p); }\n",
                i, c_type(it), c_type(it), c_type(it), c_type(it),
                copy_into(it, "(&_c->arena)", sfmt("%s", "_v")));
        fprintf(o, "static int tycho_chan_recv_%d(HChan *_ch, Arena *_dst, %s*_out) { "
                   "HCell *_c = tycho_chan_recv_cell(_ch); if (!_c) return 0; "
                   "*_out = %s; tycho_chan_recv_commit(_ch, _c); return 1; }\n",
                i, c_type(it),
                copy_into(it, "_dst", sfmt("(*(%s*)_c->val)", c_type(it))));
        /* select arm: 1 = got (value copied out), 0 = open but empty, 2 = closed + drained */
        fprintf(o, "static int tycho_chan_tryrecv_%d(HChan *_ch, Arena *_dst, %s*_out) { "
                   "HCell *_c; int _st = tycho_chan_try_recv(_ch, &_c); if (_st != 1) return _st; "
                   "*_out = %s; tycho_chan_recv_commit(_ch, _c); return 1; }\n",
                i, c_type(it),
                copy_into(it, "_dst", sfmt("(*(%s*)_c->val)", c_type(it))));
    }
    if (g_nchantypes) fputs("\n", o);
    for (int k = 0; k < g_nfnval; k++) {   /* fat-value thunks: <name>__clo wraps h_<name>, ignoring env */
        Sig *fs = sig_find(g_fnval[k]);
        if (!fs) continue;
        fprintf(o, "static %s%s__clo(void *_e, Arena *_p", c_type(fs->ret), g_fnval[k]);
        for (int j = 0; j < fs->nparams; j++) fprintf(o, ", %sa%d", c_type(fs->params[j]), j);
        fprintf(o, ") { (void)_e; %sh_%s(_p", fs->ret == T_VOID ? "" : "return ", g_fnval[k]);
        for (int j = 0; j < fs->nparams; j++) fprintf(o, ", a%d", j);
        fprintf(o, "); }\n");
    }
    for (int i = 0; i < g_nlaminfo; i++) {   /* closure thunks: unpack the env, call the lifted proc */
        LamInfo *li = &g_laminfo[i];
        if (li->ftype == T_VOID) continue;
        Proc *pr = li->proc;
        int nlam = pr->nparams - li->ncap;
        fprintf(o, "static %s__lam%d__clo(void *_env, Arena *_p", c_type(pr->ret), i);
        for (int j = 0; j < nlam; j++) fprintf(o, ", %sa%d", c_type(pr->params[li->ncap + j].type), j);
        fprintf(o, ") {");
        if (li->ncap > 0) fprintf(o, " Env_%d *_e = (Env_%d *)_env;", i, i);
        else fprintf(o, " (void)_env;");
        fprintf(o, " %sh___lam%d(_p", pr->ret == T_VOID ? "" : "return ", i);   /* gen_proc prefixes the proc name with h_ */
        for (int j = 0; j < li->ncap; j++) fprintf(o, ", _e->c%d", j);
        for (int j = 0; j < nlam; j++) fprintf(o, ", a%d", j);
        fprintf(o, "); }\n");
    }
    if (g_nfnval || g_nlaminfo) fputs("\n", o);
    for (int i = 0; i < prog->n; i++) if (!prog->v[i]->is_extern && !prog->v[i]->generic) gen_proc(o, prog->v[i]);   /* FFI externs have no body; generic templates emit instances below */
    for (int i = 0; i < g_nlaminfo; i++)   /* lifted lambda bodies */
        if (g_laminfo[i].ftype != T_VOID) gen_proc(o, g_laminfo[i].proc);
    for (int i = 0; i < g_parprocs.n; i++)   /* lifted parallel-for chunk bodies (CC-3) */
        gen_proc(o, g_parprocs.v[i]);
    /* generics: emit each monomorphic instance body. All instances were resolved
     * up front (so a nested-generic instance already has its prototype above) and
     * gen_proc is self-contained, so this is emit-only. */
    for (int i = 0; i < g_nginsts; i++)
        gen_proc(o, g_inst_procs[i]);
    fputs("int main(int argc, char **argv) {\n", o);
    fputs("    tycho_hash_seed_init();  /* random per-process map-hash seed, before any map use */\n", o);
    fputs("    tycho_argc = argc; tycho_argv = argv;  /* exposed to the program via args() */\n", o);
    fputs("    Arena _root = arena_new(0);  /* root arena; default block size */\n", o);
    fputs("    h_main(&_root);\n", o);
    fputs("    arena_free(&_root);\n", o);
    fputs("    return 0;\n}\n", o);
}

/* ---------------------------------------------------------------- main */

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "tychoc: cannot open %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)xmalloc((size_t)sz + 1);
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { fprintf(stderr, "tychoc: read error\n"); exit(1); }
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

/* directory of a path: "proj/geom/point.ty" -> "proj/geom"; "main.ty" -> "." */
static char *path_dir(const char *p) {
    const char *slash = strrchr(p, '/');
    return slash ? xstrndup(p, (size_t)(slash - p)) : xstrndup(".", 1);
}
/* leading `package <name>` of a token stream, or NULL if the file has none */
static const char *detect_package(Tok *toks) {
    int i = 0;
    while (toks[i].kind == TK_NEWLINE) i++;
    if (toks[i].kind == TK_IDENT && !strcmp(toks[i].text, "package") && toks[i + 1].kind == TK_IDENT)
        return toks[i + 1].text;
    return NULL;
}
static int pkg_file_cmp(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}
/* Import-graph traversal state. Packages are keyed by their canonical (realpath)
 * directory so the same package reached two ways is compiled once, and a back
 * edge to a package still being traversed is a cycle error. */
static char **g_pkg_seen;   static int g_npkg_seen   = 0, g_pkg_seen_cap = 0;   /* fully merged */
static char *g_pkg_active[64];  static int g_npkg_active = 0;   /* on the current DFS path */

static char *canon_dir(const char *dir) {
    char *r = realpath(dir, NULL);
    return r ? r : xstrndup(dir, strlen(dir));
}

/* Scan a lexed file's header for its import paths. The grammar puts every
 * import after the optional `package` decl and before any definition, so a
 * cheap token walk (no full parse, no type interning) suffices. Used to drive
 * post-order package loading: an imported package must be fully parsed — its
 * types registered — before the importer parses signatures that name them
 * (`p: geom.Point`), because type references intern to numeric ids eagerly. */
static void scan_imports(Tok *t, char **paths, int *n, int max) {
    int i = 0;
    while (t[i].kind == TK_NEWLINE) i++;
    if (t[i].kind == TK_IDENT && !strcmp(t[i].text, "package")) {
        i++;
        if (t[i].kind == TK_IDENT) i++;
    }
    for (;;) {
        while (t[i].kind == TK_NEWLINE) i++;
        if (t[i].kind == TK_IDENT && !strcmp(t[i].text, "import")) {
            i++;
            if (t[i].kind == TK_IDENT) i++;          /* optional alias */
            if (t[i].kind == TK_STR) {
                if (*n < max) paths[(*n)++] = t[i].text;
                i++;
            }
        } else break;
    }
}

/* Load package `pkgname` (mangled with `prefix`, "" for the main package): lex
 * every .ty in `dir`, load its imports FIRST (post-order, paths relative to
 * `dir`), then full-parse this package's files and append the defs to *prog.
 * Each imported package's name is its import path's last component and its
 * files must declare exactly that (the dir match is structural); the main
 * package's directory may be named anything (it is reached by the entry file). */
/* Scan a package directory for .ty files into files[512], sorted. Shared by
 * merge_pkg and bundle_pkg; `toomany_msg` keeps each caller's exact error text. */
static int scan_pkg_files(const char *dir, char **files, const char *toomany_msg) {
    DIR *d = opendir(dir);
    if (!d) { fprintf(stderr, "tychoc: cannot open package directory %s\n", dir); exit(1); }
    int nf = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        const char *nm = de->d_name;
        size_t L = strlen(nm);
        if (L > 3 && !strcmp(nm + L - 3, ".ty")) {
            if (nf >= 512) { fprintf(stderr, "tychoc: %s %s\n", toomany_msg, dir); exit(1); }
            files[nf++] = sfmt("%s/%s", dir, nm);
        }
    }
    closedir(d);
    if (nf == 0) { fprintf(stderr, "tychoc: package directory %s has no .ty files\n", dir); exit(1); }
    qsort(files, (size_t)nf, sizeof(char *), pkg_file_cmp);
    return nf;
}

/* Shared DFS prologue/epilogue for the two package walkers (merge_pkg and
 * bundle_pkg, which duplicated it): cycle + already-merged checks, depth cap,
 * push onto the active path. Returns 0 when the package is already merged
 * (caller does nothing); on 1, *keyout owns the canonical key — pass it to
 * pkg_walk_done after processing. `desc` names the package in the cycle error. */
static int pkg_walk_enter(const char *dir, const char *desc, char **keyout) {
    char *key = canon_dir(dir);
    for (int i = 0; i < g_npkg_active; i++)
        if (!strcmp(g_pkg_active[i], key)) {
            fprintf(stderr, "tychoc: import cycle through %s\n", desc);
            exit(1);
        }
    for (int i = 0; i < g_npkg_seen; i++)
        if (!strcmp(g_pkg_seen[i], key)) { free(key); return 0; }   /* shared dep already merged */
    if (g_npkg_active >= 64) { fprintf(stderr, "tychoc: package nesting too deep\n"); exit(1); }
    g_pkg_active[g_npkg_active++] = key;
    *keyout = key;
    return 1;
}
static void pkg_walk_done(char *key) {
    g_npkg_active--;                       /* pop the DFS path */
    TBL_ENSURE(g_pkg_seen, g_npkg_seen, g_pkg_seen_cap);
    g_pkg_seen[g_npkg_seen++] = key;       /* mark merged */
}

static void merge_pkg(const char *dir, const char *pkgname, const char *prefix, ProcVec *prog) {
    char *key;
    if (!pkg_walk_enter(dir, sfmt("package `%s` (%s)", pkgname, dir), &key)) return;

    /* FFI: a co-located `<pkg>_shim.c` is auto-compiled+linked (turnkey C-backed
     * modules, e.g. core:regex over <regex.h>). One per package; deduped. */
    char *shimc = sfmt("%s/%s_shim.c", dir, pkgname);
    if (file_exists(shimc)) { add_shim(shimc); add_pkg_deps(dir); }

    char *files[512];
    int nf = scan_pkg_files(dir, files, "too many files in package");

    /* lex every file once; collect this package's imports from the headers */
    TokVec toks[512];
    char *imp_paths[256]; int n_imp = 0;
    for (int i = 0; i < nf; i++) {
        g_srcname = files[i];
        char *s = read_file(files[i]);
        toks[i] = lex(s);
        scan_imports(toks[i].v, imp_paths, &n_imp, 256);
    }

    /* load imported packages first (post-order; still on the active path => cycles caught) */
    for (int k = 0; k < n_imp; k++) {
        const char *path = imp_paths[k];
        const char *childname = pkg_basename(path);
        char *childdir    = resolve_pkg_dir(dir, path);
        char *childprefix = sfmt("%s__", childname);
        merge_pkg(childdir, childname, childprefix, prog);
    }

    /* now full-parse this package's files: imported types are registered */
    for (int i = 0; i < nf; i++) {
        g_srcname = files[i];
        g_cur_pkg_prefix = prefix;
        ProcVec pv = parse_program(toks[i].v);
        if (!g_parsed_package) {
            fprintf(stderr, "tychoc: %s is in package `%s` but has no `package` declaration\n", files[i], pkgname);
            exit(1);
        }
        if (strcmp(g_parsed_package, pkgname) != 0) {
            fprintf(stderr, "tychoc: %s declares `package %s` but is in package `%s`\n", files[i], g_parsed_package, pkgname);
            exit(1);
        }
        for (int j = 0; j < pv.n; j++) {
            if (prog->n == prog->cap) {
                prog->cap = prog->cap ? prog->cap * 2 : 8;
                prog->v = (Proc **)xrealloc(prog->v, (size_t)prog->cap * sizeof(Proc *));
            }
            prog->v[prog->n++] = pv.v[j];
        }
    }
    g_cur_pkg_prefix = "";

    pkg_walk_done(key);
}

/* Compile a package program: start at the entry file's package (prefix "") and
 * follow the import graph. */
static ProcVec compile_package(const char *entry, const char *pkgname) {
    ProcVec prog = {0};
    char *dir = path_dir(entry);
    merge_pkg(dir, pkgname, "", &prog);
    return prog;
}

/* --bundle: print the post-order concatenation of a package program's source
 * (each file keeps its `package`/`import` headers) as one stream — the input
 * format for the stdin-only self-hosted compiler (tychoc0), whose parser switches
 * its mangling prefix on each `package` header. Same traversal/ordering as
 * merge_pkg (imports first), so tychoc0 sees definitions in dependency order. */
/* Emit a file, rewriting its leading `package <name>` header line to
 * `package main`. The entry package keeps the empty mangling prefix in tychoc0
 * (which maps `package main` -> no prefix) regardless of its source name, just
 * as tychoc gives the entry package prefix "". */
static void emit_entry_file(const char *c) {
    const char *line = c;
    while (*line) {
        const char *eol = line; while (*eol && *eol != '\n') eol++;
        if (!strncmp(line, "package ", 8)) {
            fwrite(c, 1, (size_t)(line - c), stdout);   /* anything before the header (comments) */
            fputs("package main", stdout);
            fputs(eol, stdout);                          /* the newline onward */
            return;
        }
        line = *eol ? eol + 1 : eol;
    }
    fputs(c, stdout);   /* no header found — emit verbatim */
}

static void bundle_pkg(const char *dir, int is_entry) {
    char *key;
    if (!pkg_walk_enter(dir, dir, &key)) return;

    char *files[512];
    int nf = scan_pkg_files(dir, files, "too many files in");

    char *imp_paths[256]; int n_imp = 0;
    for (int i = 0; i < nf; i++) {
        char *s = read_file(files[i]);
        TokVec t = lex(s);
        scan_imports(t.v, imp_paths, &n_imp, 256);
    }
    for (int k = 0; k < n_imp; k++)
        bundle_pkg(resolve_pkg_dir(dir, imp_paths[k]), 0);   /* core: -> $TYCHO_CORELIB; else relative */
    for (int i = 0; i < nf; i++) {
        if (is_entry) emit_entry_file(read_file(files[i]));   /* entry -> `package main` (prefix "") */
        else          fputs(read_file(files[i]), stdout);
        fputc('\n', stdout);
    }
    pkg_walk_done(key);
}

/* A library/package name reaches a shell command line (the cc `system()` link
 * line and the `pkg-config` popen) from `extern "Lib"` in the .ty SOURCE and from
 * --link/--pkg. If it carried shell metacharacters, compiling an untrusted .ty
 * (e.g. `extern "x; rm -rf ~"`) would execute arbitrary shell. Restrict every such
 * name to a conservative cc-token charset and fail closed -- a real library name
 * never needs anything outside [A-Za-z0-9._+-]. */
static const char *cc_safe_name(const char *s, const char *what) {
    if (!s || !*s) { fprintf(stderr, "tychoc: empty %s name\n", what); exit(1); }
    for (const char *p = s; *p; p++) {
        char c = *p;
        int ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                 (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '+' || c == '-';
        if (!ok) { fprintf(stderr, "tychoc: illegal character in %s name '%s' (only [A-Za-z0-9._+-] allowed)\n", what, s); exit(1); }
    }
    return s;
}

/* FFI Stage 3: `pkg-config --cflags --libs <name>` -> the cc flags for a system
 * library, or NULL on failure. The result is spliced onto the cc line. */
static char *pkg_config_flags(const char *name) {
    cc_safe_name(name, "--pkg");   /* name reaches the shell below; reject metacharacters */
    char *cmd = sfmt("pkg-config --cflags --libs %s 2>/dev/null", name);
    FILE *p = popen(cmd, "r");
    if (!p) return NULL;
    char buf[4096];
    size_t n = fread(buf, 1, sizeof buf - 1, p);
    int rc = pclose(p);
    if (rc != 0) return NULL;
    buf[n] = 0;
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ')) buf[--n] = 0;
    return xstrndup(buf, n);
}

/* --symbols: a TAB-separated symbol index for the language server (hover +
 * go-to-definition). Emitted post-resolve so types are concrete. Records:
 *   fn<TAB>name<TAB>line<TAB>signature
 *   param<TAB>name<TAB>type<TAB>fn-line          (scope = the enclosing fn)
 *   struct<TAB>name<TAB>line   /  field<TAB>struct<TAB>name<TAB>type
 *   enum<TAB>name<TAB>line     /  variant<TAB>enum<TAB>name
 *   type<TAB>name<TAB>underlying
 * Locals (`:=`) are intentionally out of scope here (they need body-level type
 * inference); params + signatures + members cover the common hover cases. */
/* Recursively emit `local<TAB>name<TAB>type<TAB>fn-line<TAB>decl-line` for every
 * S_DECL / S_MDECL binding in a body (descending into if/while/for/match blocks),
 * scoped to the enclosing function's start line. decl_type/mtypes are already
 * resolved post-resolve_program, so the type is the inferred one. (Loop vars and
 * match-arm binds aren't covered yet -- foreach desugars to an S_DECL so it is.) */
static void emit_locals(Stmt **body, int n, int fnline) {
    for (int i = 0; i < n; i++) {
        Stmt *s = body[i];
        if (!s) continue;
        if (s->kind == S_DECL)
            printf("local\t%s\t%s\t%d\t%d\n", s->name, type_name(s->decl_type), fnline, s->line);
        else if (s->kind == S_MDECL)
            for (int j = 0; j < s->nnames; j++)
                printf("local\t%s\t%s\t%d\t%d\n", s->names[j], type_name(s->mtypes[j]), fnline, s->line);
        if (s->body) emit_locals(s->body, s->nbody, fnline);
        if (s->els)  emit_locals(s->els, s->nels, fnline);
        for (int a = 0; a < s->narms; a++)
            emit_locals(s->arms[a].body, s->arms[a].nbody, fnline);
    }
}

static void emit_symbols(ProcVec *prog) {
    for (int i = 0; i < prog->n; i++) {
        Proc *p = prog->v[i];
        char *sig = sfmt("fn %s(", p->name);
        for (int j = 0; j < p->nparams; j++)
            sig = sfmt("%s%s%s: %s", sig, j ? ", " : "", p->params[j].name, type_name(p->params[j].type));
        sig = sfmt("%s)", sig);
        if (p->has_ret) sig = sfmt("%s -> %s", sig, type_name(p->ret));
        printf("fn\t%s\t%d\t%s\n", p->name, p->line, sig);
        for (int j = 0; j < p->nparams; j++)
            printf("param\t%s\t%s\t%d\n", p->params[j].name, type_name(p->params[j].type), p->line);
        emit_locals(p->body, p->nbody, p->line);
    }
    for (int i = 0; i < g_nstructs; i++) {
        StructDef *s = &g_structs[i];
        printf("struct\t%s\t%d\n", s->name, s->line);
        for (int j = 0; j < s->nfields; j++)
            printf("field\t%s\t%s\t%s\n", s->name, s->fields[j].name, type_name(s->fields[j].type));
    }
    for (int i = 0; i < g_nenums; i++) {
        EnumDef *e = &g_enums[i];
        printf("enum\t%s\t%d\n", e->name, e->line);
        for (int j = 0; j < e->nvariants; j++)
            printf("variant\t%s\t%s\n", e->name, e->variants[j].name);
    }
    for (int i = 0; i < g_nnewtypes; i++)
        printf("type\t%s\t%s\n", g_newtypes[i].name, type_name(g_newtypes[i].under));
}

/* C-string-escape a source path for a `#line` directive (backslash + quote). */
static char *c_escape_path(const char *p) {
    size_t n = strlen(p);
    char *b = xmalloc(2 * n + 1);
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        if (p[i] == '\\' || p[i] == '"') b[k++] = '\\';
        b[k++] = p[i];
    }
    b[k] = 0;
    return b;
}

int main(int argc, char **argv) {
    g_argv0 = argv[0];
    const char *input = NULL;
    const char *out   = NULL;
    const char *cc    = "cc";
    int emit_c_only = 0;
    int want_symbols = 0;
    int bundle = 0;
    int debug = 0;    /* -g: emit #line directives + build with -O0 -g (single-file only) */
    int native = 0;   /* --native: add -march=native (non-portable: SIGILL on a different CPU) */
    char *extra = sfmt("%s", "");   /* FFI: extra cc link/include flags (-L/-I/--link/--pkg) */
    char *shims = sfmt("%s", "");   /* FFI: companion C shim sources (--shim) compiled+linked alongside */

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--emit-c")) emit_c_only = 1;
        else if (!strcmp(argv[i], "--symbols")) want_symbols = 1;
        else if (!strcmp(argv[i], "--bundle")) bundle = 1;
        else if (!strcmp(argv[i], "--native")) native = 1;
        else if (!strcmp(argv[i], "-g")) debug = 1;
        else if (!strcmp(argv[i], "--cc") && i + 1 < argc) cc = argv[++i];
        /* FFI Stage 3: linker/include ergonomics. -L/-I accept both attached
         * (-L/path) and separated (-L /path) forms; all accumulate onto the cc line. */
        else if (!strncmp(argv[i], "-L", 2) || !strncmp(argv[i], "-I", 2)) {
            extra = sfmt("%s %s", extra, argv[i]);
            if (!argv[i][2] && i + 1 < argc) extra = sfmt("%s %s", extra, argv[++i]);
        }
        else if (!strcmp(argv[i], "--link") && i + 1 < argc) extra = sfmt("%s -l%s", extra, cc_safe_name(argv[++i], "--link"));
        else if (!strcmp(argv[i], "--shim") && i + 1 < argc) shims = sfmt("%s %s", shims, argv[++i]);
        else if (!strcmp(argv[i], "--pkg") && i + 1 < argc) {
            char *pc = pkg_config_flags(argv[++i]);
            if (!pc) { fprintf(stderr, "tychoc: pkg-config failed for '%s'\n", argv[i]); return 1; }
            extra = sfmt("%s %s", extra, pc);
        }
        else if (argv[i][0] == '-') { fprintf(stderr, "tychoc: unknown flag %s\n", argv[i]); return 1; }
        else input = argv[i];
    }
    if (!input) {
        fprintf(stderr, "usage: tychoc file.ty [-o name] [--emit-c] [-g] [--bundle] [--native] [--cc <compiler>]\n"
                        "                     [-L<dir>] [-I<dir>] [--link <lib>] [--shim <file.c>] [--pkg <name>]\n");
        return 1;
    }
    g_srcname = input;

    if (bundle) {   /* emit the package's source as one post-order stream (for tychoc0) */
        bundle_pkg(path_dir(input), 1);
        return 0;
    }

    char *base   = out ? xstrndup(out, strlen(out)) : strip_ext(input);
    char *c_path = sfmt("%s.c", base);

    char *src = read_file(input);
    TokVec toks = lex(src);
    const char *pkg = detect_package(toks.v);
    ProcVec prog = pkg ? compile_package(input, pkg)   /* package: merge the whole directory */
                       : parse_program(toks.v);        /* single file: unchanged */
    if (debug && !pkg) {   /* -g: line info only for single-file builds -- merged packages lose per-node filenames */
        g_line_info = 1;
        g_line_file = c_escape_path(input);
    } else if (debug && pkg) {
        fprintf(stderr, "tychoc: -g line info is emitted only for single-file compiles; skipped for this package build\n");
    }
    check_finite_types();   /* reject by-value-recursive types before the resolver */
    resolve_program(&prog);

    if (want_symbols) { emit_symbols(&prog); return 0; }   /* LSP index; no codegen */

    FILE *o = fopen(c_path, "wb");
    if (!o) { fprintf(stderr, "tychoc: cannot write %s\n", c_path); return 1; }
    gen_program(o, &prog);
    fclose(o);

    if (emit_c_only) {
        printf("wrote %s\n", c_path);
        return 0;
    }

    char *links = sfmt("%s", "");                  /* FFI: -lLib for each `extern "Lib"` */
    for (int i = 0; i < g_nlinks; i++) links = sfmt("%s -l%s", links, cc_safe_name(g_links[i], "extern library"));
    /* sources (generated .c + any --shim companions), then -lm + extern libs +
     * the -L/-I/--link/--pkg passthrough (libs trail the objects that need them). */
    /* -O3 is the portable default; --native opts into -march=native (host-CPU only).
     * -fwrapv: signed integer overflow is DEFINED as two's-complement wrapping
     * (not C UB), so the optimizer can never miscompile overflowing arithmetic.
     * This is the language's integer-overflow contract; see docs/internals. */
    const char *march = native ? " -march=native" : "";
    const char *optdbg = debug ? "-O0 -g" : "-O3";   /* -g: unoptimized + DWARF so gdb/lldb step the .ty source */
    for (int i = 0; i < g_nshims; i++) shims = sfmt("%s %s", shims, g_shims[i]);   /* auto-discovered <pkg>_shim.c */
    const char *pkgdeps = g_pkgdeps ? g_pkgdeps : "";   /* pkg-config flags from <pkg>/deps (cflags + libs, trailing) */
    char *cmd = sfmt("%s %s -fwrapv%s -pthread -o %s %s%s -lm%s%s %s", cc, optdbg, march, base, c_path, shims, links, extra, pkgdeps);
    int rc = system(cmd);
    if (rc != 0) { fprintf(stderr, "tychoc: C compilation failed (%s)\n", cmd); return 1; }
    printf("built %s\n", base);
    return 0;
}
