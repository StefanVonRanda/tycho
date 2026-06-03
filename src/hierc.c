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
#include <dirent.h>

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
    TK_IDENT, TK_INT, TK_FLOAT, TK_STR, TK_CHAR,
    TK_COLONCOLON, TK_COLONEQ, TK_COLON, TK_EQ,
    TK_EQEQ, TK_NEQ, TK_LT, TK_GT, TK_LE, TK_GE,
    TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH,
    TK_LPAREN, TK_RPAREN, TK_LBRACKET, TK_RBRACKET, TK_COMMA, TK_ARROW,
    TK_FN, TK_RETURN, TK_IF, TK_ELIF, TK_ELSE, TK_FOR, TK_IN, TK_TRUE, TK_FALSE, TK_STRUCT,
    TK_INOUT, TK_AMP, TK_AND, TK_OR, TK_NOT, TK_MATCH, TK_ENUM, TK_ORRETURN, TK_TYPE,
    TK_DOT,
    TK_KW_INT, TK_KW_BOOL, TK_KW_STRING, TK_KW_FLOAT
} TokKind;

typedef struct {
    TokKind kind;
    char   *text;   /* identifier name, or raw string contents */
    long    ival;
    int     line;
    double  fval;   /* TK_FLOAT literal value */
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
    if (!strcmp(s, "elif"))   return TK_ELIF;
    if (!strcmp(s, "else"))   return TK_ELSE;
    if (!strcmp(s, "and"))    return TK_AND;
    if (!strcmp(s, "or_return")) return TK_ORRETURN;   /* before "or": longer match wins anyway, but explicit */
    if (!strcmp(s, "or"))     return TK_OR;
    if (!strcmp(s, "not"))    return TK_NOT;
    if (!strcmp(s, "match"))  return TK_MATCH;
    if (!strcmp(s, "for"))    return TK_FOR;
    if (!strcmp(s, "in"))     return TK_IN;
    if (!strcmp(s, "struct")) return TK_STRUCT;
    if (!strcmp(s, "enum"))   return TK_ENUM;
    if (!strcmp(s, "type"))   return TK_TYPE;
    if (!strcmp(s, "inout"))  return TK_INOUT;
    if (!strcmp(s, "true"))   return TK_TRUE;
    if (!strcmp(s, "false"))  return TK_FALSE;
    if (!strcmp(s, "int"))    return TK_KW_INT;
    if (!strcmp(s, "bool"))   return TK_KW_BOOL;
    if (!strcmp(s, "string")) return TK_KW_STRING;
    if (!strcmp(s, "float"))  return TK_KW_FLOAT;
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
            tv_push(&out, (Tok){TK_INDENT, NULL, 0, line, 0});
        } else {
            while (col < indent_stack[sp]) {
                sp--;
                tv_push(&out, (Tok){TK_DEDENT, NULL, 0, line, 0});
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
                const char *s = p;
                while (isdigit((unsigned char)*p)) p++;
                /* a '.' immediately followed by a digit makes it a float (D.D);
                 * otherwise the '.' is a separate token (e.g. struct field). No
                 * exponent or leading-dot form yet. */
                if (*p == '.' && isdigit((unsigned char)p[1])) {
                    p++;
                    while (isdigit((unsigned char)*p)) p++;
                    double dv = strtod(s, NULL);
                    tv_push(&out, (Tok){TK_FLOAT, NULL, 0, line, dv});
                } else {
                    long v = 0;
                    for (const char *q = s; q < p; q++) v = v * 10 + (*q - '0');
                    tv_push(&out, (Tok){TK_INT, NULL, v, line, 0});
                }
                continue;
            }
            if (isalpha((unsigned char)c) || c == '_') {
                const char *s = p;
                while (isalnum((unsigned char)*p) || *p == '_') p++;
                char *name = xstrndup(s, (size_t)(p - s));
                TokKind k = keyword(name);
                tv_push(&out, (Tok){k, name, 0, line, 0});
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
                tv_push(&out, (Tok){TK_STR, xstrndup(buf, (size_t)bn), 0, line, 0});
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
                tv_push(&out, (Tok){TK_CHAR, NULL, cv, line, 0});
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
            tv_push(&out, (Tok){k, NULL, 0, line, 0});
            p += len;
        }

        tv_push(&out, (Tok){TK_NEWLINE, NULL, 0, line, 0});
        if (*p == '#') while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }

    /* close out remaining indentation, then EOF */
    while (sp > 0) { sp--; tv_push(&out, (Tok){TK_DEDENT, NULL, 0, line, 0}); }
    tv_push(&out, (Tok){TK_EOF, NULL, 0, line, 0});
    return out;
}

/* ------------------------------------------------------------------ AST */

/* Type is an int so a struct id can be encoded in it: values >=
 * T_STRUCT_BASE name a struct (id = value - base). The primitive enum
 * constants keep working in every existing == and switch. */
typedef int Type;
enum { T_VOID, T_INT, T_BOOL, T_STRING, T_ARRAY_INT, T_ARRAY_STRING, T_MAP_SI, T_FLOAT, T_ARRAY_FLOAT,
       T_MAP_SF /* [string: float] */,
       T_NONE, /* type of a bare `None` until context fixes its concrete Option type */
       /* Ok(v)/Err(e) each know only ONE of Result's two type params, so they
        * carry a partial sentinel (the known inner type sits on the value's lhs)
        * until context fixes the full Result type — the same trick as T_NONE. */
       T_OK_PARTIAL, T_ERR_PARTIAL,
       T_CHAR /* one byte; represented as `long` in C, prints as a char via string append */ };
#define T_STRUCT_BASE   64
/* structs occupy [64, T_ARRC_BASE); composite arrays sit above that (both are
 * >= 64, so the upper bound is what keeps an array type from looking like a
 * struct). */
#define IS_STRUCT(t)    ((t) >= T_STRUCT_BASE && (t) < T_ARRC_BASE)
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

/* Composite array types — arrays whose element is a struct or another array
 * ([Point], [[int]], ...). Unlike [int]/[float]/[string] (fixed enum values
 * with hand-written runtime), these are interned in a side table (mirroring
 * struct interning) and their runtime type + ops are GENERATED, one monomorphic
 * HierArrC<id> per distinct element type used. Ids start above the struct
 * range; the element is interned before its container, so id order is a valid
 * emit order. */
#define T_ARRC_BASE 1024
#define T_OPT_BASE  4096   /* defined here so IS_ARRC's upper bound can reference it */
#define T_RES_BASE  6144   /* Result(T,E), between the Option and enum ranges */
#define T_ENUM_BASE 8192   /* user sum types, above the Result range */
#define T_TUP_BASE  16384  /* tuples (T1, ..., Tn), above the (now bounded) enum range */
#define T_NT_BASE   24576  /* distinct newtypes (type X = int/float), above tuples */
typedef struct { Type elem; } ArrType;
static ArrType g_arrtypes[256];
static int g_narrtypes = 0;
#define IS_ARRC(t)  ((t) >= T_ARRC_BASE && (t) < T_OPT_BASE)   /* options sit above */
#define ARRC_ID(t)  ((int)((t) - T_ARRC_BASE))
static Type arrc_of(Type elem) {                 /* find-or-create [elem] */
    for (int i = 0; i < g_narrtypes; i++)
        if (g_arrtypes[i].elem == elem) return T_ARRC_BASE + i;
    if (g_narrtypes >= 256) { fprintf(stderr, "hierc: too many array types\n"); exit(1); }
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

/* Option(T) — a tagged optional (Some(value) or None). Interned like composite
 * arrays; one monomorphic HierOpt<id> { char has; T val; } is generated per
 * inner type used. Ids sit above the array range (T_OPT_BASE, defined above). */
typedef struct { Type inner; } OptType;
static OptType g_opttypes[256];
static int g_nopttypes = 0;
#define IS_OPT(t)  ((t) >= T_OPT_BASE && (t) < T_RES_BASE)   /* Results sit above */
#define OPT_ID(t)  ((int)((t) - T_OPT_BASE))
static Type opt_of(Type inner) {                 /* find-or-create Option(inner) */
    for (int i = 0; i < g_nopttypes; i++)
        if (g_opttypes[i].inner == inner) return T_OPT_BASE + i;
    if (g_nopttypes >= 256) { fprintf(stderr, "hierc: too many option types\n"); exit(1); }
    g_opttypes[g_nopttypes].inner = inner;
    return T_OPT_BASE + g_nopttypes++;
}
static Type opt_inner(Type t) { return g_opttypes[OPT_ID(t)].inner; }

/* Result(T, E) — a tagged success-or-failure (Ok(value) or Err(error)). The
 * no-exceptions error story: a function returns Result(T, E) and the caller
 * matches Ok/Err. Interned like Option, but over TWO inner types; one
 * monomorphic HierRes<id> { char ok; T okv; E errv; } is generated per (T,E)
 * pair used. Ids sit in [T_RES_BASE, T_ENUM_BASE). */
typedef struct { Type ok; Type err; } ResType;
static ResType g_restypes[256];
static int g_nrestypes = 0;
#define IS_RES(t)  ((t) >= T_RES_BASE && (t) < T_ENUM_BASE)
#define RES_ID(t)  ((int)((t) - T_RES_BASE))
static Type res_of(Type ok, Type err) {          /* find-or-create Result(ok, err) */
    for (int i = 0; i < g_nrestypes; i++)
        if (g_restypes[i].ok == ok && g_restypes[i].err == err) return T_RES_BASE + i;
    if (g_nrestypes >= 256) { fprintf(stderr, "hierc: too many result types\n"); exit(1); }
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
typedef struct { char *name; Variant variants[64]; int nvariants; int line; } EnumDef;
static EnumDef g_enums[64];
static int g_nenums = 0;
#define IS_ENUM(t)    ((t) >= T_ENUM_BASE && (t) < T_TUP_BASE)
#define ENUM_ID(t)    ((int)((t) - T_ENUM_BASE))
#define ENUM_TYPE(id) (T_ENUM_BASE + (id))
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
 * HierTup<id> { T0 _0; ...; Tn-1 _n-1; } is generated per distinct element-type
 * list. Ids sit at [T_TUP_BASE, ...). Deep-copied by value field-wise. */
typedef struct { Type elems[8]; int n; } TupType;
static TupType g_tuptypes[256];
static int g_ntuptypes = 0;
#define IS_TUP(t)  ((t) >= T_TUP_BASE && (t) < T_NT_BASE)
#define TUP_ID(t)  ((int)((t) - T_TUP_BASE))
static Type tup_of(Type *elems, int n) {         /* find-or-create (elems...) */
    for (int i = 0; i < g_ntuptypes; i++)
        if (g_tuptypes[i].n == n) {
            int same = 1;
            for (int j = 0; j < n; j++) if (g_tuptypes[i].elems[j] != elems[j]) { same = 0; break; }
            if (same) return T_TUP_BASE + i;
        }
    if (g_ntuptypes >= 256) { fprintf(stderr, "hierc: too many tuple types\n"); exit(1); }
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
static NewtypeDef g_newtypes[128];
static int g_nnewtypes = 0;
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
static SoaType g_soatypes[256];
static int g_nsoatypes = 0;
#define IS_SOA(t)   ((t) >= T_SOA_BASE)
#define SOA_ID(t)   ((int)((t) - T_SOA_BASE))
static Type soa_of(Type st) {                   /* find-or-create soa [st] */
    for (int i = 0; i < g_nsoatypes; i++)
        if (g_soatypes[i].st == st) return T_SOA_BASE + i;
    if (g_nsoatypes >= 256) { fprintf(stderr, "hierc: too many soa types\n"); exit(1); }
    g_soatypes[g_nsoatypes].st = st;
    return T_SOA_BASE + g_nsoatypes++;
}
static Type soa_struct(Type t) { return g_soatypes[SOA_ID(t)].st; }

/* String-keyed maps come in two value flavours: [string: int] (HierMapSI) and
 * [string: float] (HierMapSF). map_fn picks the runtime infix, map_val the
 * value type, map_of the map type for a value type. */
static int is_map(Type t) { return t == T_MAP_SI || t == T_MAP_SF; }
static const char *map_fn(Type t) { return t == T_MAP_SF ? "sf" : "si"; }
static Type map_val(Type t) { return t == T_MAP_SF ? T_FLOAT : T_INT; }
static Type map_of(Type v) { return v == T_FLOAT ? T_MAP_SF : T_MAP_SI; }

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
    if (t == T_STRING || is_map(t) || is_array(t)) return 1;
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
    if (IS_ARRC(t))   return sfmt("HierArrC%d ", ARRC_ID(t));
    if (IS_OPT(t))    return sfmt("HierOpt%d ", OPT_ID(t));
    if (IS_RES(t))    return sfmt("HierRes%d ", RES_ID(t));
    if (IS_TUP(t))    return sfmt("HierTup%d ", TUP_ID(t));
    if (IS_ENUM(t))   return sfmt("E_%s *", g_enums[ENUM_ID(t)].name);   /* a value is a pointer to a tagged cell */
    if (IS_SOA(t))    return sfmt("Soa%d ", SOA_ID(t));
    switch (t) {
        case T_INT:          return "long ";
        case T_CHAR:         return "long ";
        case T_FLOAT:        return "double ";
        case T_BOOL:         return "int ";
        case T_STRING:       return "char *";
        case T_ARRAY_INT:    return "HierArrInt ";
        case T_ARRAY_FLOAT:  return "HierArrFloat ";
        case T_ARRAY_STRING: return "HierArrStr ";
        case T_MAP_SI:       return "HierMapSI ";
        case T_MAP_SF:       return "HierMapSF ";
        default:             return "void ";
    }
}
static const char *type_name(Type t) {
    if (IS_NEWTYPE(t)) return g_newtypes[NT_ID(t)].name;
    if (IS_STRUCT(t)) return g_structs[STRUCT_ID(t)].name;
    if (IS_ARRC(t))   return sfmt("[%s]", type_name(arr_elem(t)));
    if (IS_OPT(t))    return sfmt("Option(%s)", type_name(opt_inner(t)));
    if (IS_RES(t))    return sfmt("Result(%s, %s)", type_name(res_ok(t)), type_name(res_err(t)));
    if (IS_TUP(t)) {
        char *s = sfmt("(%s", type_name(tup_elem(t, 0)));
        for (int i = 1; i < tup_n(t); i++) s = sfmt("%s, %s", s, type_name(tup_elem(t, i)));
        return sfmt("%s)", s);
    }
    if (IS_ENUM(t))   return g_enums[ENUM_ID(t)].name;
    if (IS_SOA(t))    return sfmt("soa [%s]", type_name(soa_struct(t)));
    switch (t) {
        case T_NONE:         return "None";
        case T_OK_PARTIAL:   return "Ok(...)";
        case T_ERR_PARTIAL:  return "Err(...)";
        case T_INT:          return "int";
        case T_FLOAT:        return "float";
        case T_BOOL:         return "bool";
        case T_STRING:       return "string";
        case T_ARRAY_INT:    return "[int]";
        case T_ARRAY_FLOAT:  return "[float]";
        case T_ARRAY_STRING: return "[string]";
        case T_MAP_SI:       return "[string: int]";
        case T_MAP_SF:       return "[string: float]";
        default:             return "void";
    }
}

/* An array type's runtime-function infix: hier_arr_<fn>_push etc. The fixed
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
               E_SLICE     /* xs[a:b]: a sub-range view (lhs=array, rhs=lo or NULL, args[0]=hi or NULL) */ } ExprKind;

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
};

typedef enum { S_DECL, S_ASSIGN, S_RETURN, S_IF, S_WHILE, S_FORRANGE,
               S_INDEXSET, S_FIELDSET, S_EXPR, S_MATCH, S_MDECL, S_MASSIGN } StmtKind;

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
    MatchArm *arms; int narms;           /* S_MATCH */
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

/* While parsing an imported package, every top-level def name and every
 * user-type reference is prefixed with "<pkg>__" so distinct packages never
 * collide in the one flat namespace. "" for the main/entry package and for
 * single-file programs, which keeps their output byte-identical. */
static const char *g_cur_pkg_prefix = "";
static char *pkg_mangle(const char *n) {   /* identity when the prefix is empty (main) */
    return g_cur_pkg_prefix[0] ? sfmt("%s%s", g_cur_pkg_prefix, n) : (char *)n;
}
static char *pkg_prefix_for(const char *qualifier);   /* defined after the import table */

static Type parse_type(Parser *ps) {
    Tok *t = cur(ps);
    if (t->kind == TK_IDENT && !strcmp(t->text, "soa")) {   /* soa [Struct] */
        ps->p++;
        eat(ps, TK_LBRACKET, "'[' after soa");
        Type el = parse_type(ps);
        eat(ps, TK_RBRACKET, "']'");
        if (!IS_STRUCT(el)) die_at(t->line, "soa requires a struct element type, e.g. soa [Point]");
        return soa_of(el);
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
        for (int i = 0; i < n; i++)
            if (elems[i] == T_VOID) die_at(t->line, "a tuple element cannot be void");
        return tup_of(elems, n);
    }
    if (t->kind == TK_LBRACKET) {        /* [int] / [string] / [string: int] */
        ps->p++;
        Type elem = parse_type(ps);
        if (at(ps, TK_COLON)) {          /* map type: [K: V] */
            ps->p++;
            Type val = parse_type(ps);
            eat(ps, TK_RBRACKET, "']'");
            if (elem == T_STRING && (val == T_INT || val == T_FLOAT)) return map_of(val);
            die_at(t->line, "maps are [string: int] or [string: float]");
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
        const char *nm;
        if (peek(ps, 1)->kind == TK_DOT && peek(ps, 2)->kind == TK_IDENT) {
            /* qualified type `pkg.Type` -> the imported package's mangled name */
            nm = sfmt("%s%s", pkg_prefix_for(t->text), peek(ps, 2)->text);
            ps->p += 2;                  /* skip qualifier + dot; the type-name ident is consumed on a hit below */
        } else {
            nm = pkg_mangle(t->text);    /* package-local: try the current package's prefixed name */
        }
        int sid = struct_find(nm);
        if (sid >= 0) { ps->p++; return STRUCT_TYPE(sid); }
        int eid = enum_find(nm);
        if (eid >= 0) { ps->p++; return ENUM_TYPE(eid); }
        int nid = newtype_find(nm);
        if (nid >= 0) { ps->p++; return NT_TYPE(nid); }
        die_at(t->line, "unknown type '%s'", t->text);
    }
    switch (t->kind) {
        case TK_KW_INT:    ps->p++; return T_INT;
        case TK_KW_FLOAT:  ps->p++; return T_FLOAT;
        case TK_KW_BOOL:   ps->p++; return T_BOOL;
        case TK_KW_STRING: ps->p++; return T_STRING;
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

static Expr *parse_primary(Parser *ps) {
    Tok *t = cur(ps);
    if (t->kind == TK_INT)  { ps->p++; Expr *e = new_expr(E_INT, t->line);  e->ival = t->ival; return e; }
    if (t->kind == TK_CHAR) { ps->p++; Expr *e = new_expr(E_CHAR, t->line); e->ival = t->ival; return e; }
    if (t->kind == TK_FLOAT){ ps->p++; Expr *e = new_expr(E_FLOAT, t->line); e->fval = t->fval; return e; }
    if (t->kind == TK_STR)  { ps->p++; Expr *e = new_expr(E_STR, t->line);  e->sval = t->text; return e; }
    if (t->kind == TK_TRUE) { ps->p++; Expr *e = new_expr(E_BOOL, t->line); e->ival = 1; return e; }
    if (t->kind == TK_FALSE){ ps->p++; Expr *e = new_expr(E_BOOL, t->line); e->ival = 0; return e; }
    if (t->kind == TK_LPAREN) {
        ps->p++;
        Expr *first = parse_expr(ps);
        if (!at(ps, TK_COMMA)) {         /* plain grouping ( expr ) */
            eat(ps, TK_RPAREN, "')'");
            return first;
        }
        Expr *e = new_expr(E_TUPLE, t->line);   /* tuple literal (e1, e2, ...) */
        int cap = 4; e->args = (Expr **)realloc(e->args, (size_t)cap * sizeof(Expr *));
        e->args[e->nargs++] = first;
        while (accept(ps, TK_COMMA)) {
            if (at(ps, TK_RPAREN)) break;       /* trailing comma */
            if (e->nargs == cap) { cap *= 2; e->args = (Expr **)realloc(e->args, (size_t)cap * sizeof(Expr *)); }
            e->args[e->nargs++] = parse_expr(ps);
        }
        eat(ps, TK_RPAREN, "')'");
        if (e->nargs > 8) die_at(t->line, "a tuple has at most 8 elements");
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
                if (elem == T_STRING && (val == T_INT || val == T_FLOAT)) { e->ival = map_of(val); e->op = TK_COLON; }
                else die_at(t->line, "maps are [string: int] or [string: float]");
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
        if (!strcmp(t->text, "soa")) {     /* soa []Struct : an empty SOA literal */
            eat(ps, TK_LBRACKET, "'[' after soa");
            eat(ps, TK_RBRACKET, "']' (an empty soa literal is `soa []Struct`)");
            Type el = parse_type(ps);
            if (!IS_STRUCT(el)) die_at(t->line, "soa requires a struct element type, e.g. soa []Point");
            Expr *e = new_expr(E_ARRLIT, t->line);
            e->ival = soa_of(el);          /* empty: type carried to the resolver */
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
        if (at(ps, TK_LPAREN)) {           /* call */
            ps->p++;
            Expr *e = new_expr(E_CALL, t->line);
            e->sval = t->text;
            e->pkg  = g_cur_pkg_prefix;   /* the package this call appears in; resolver tries <pkg>name first */
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
        Expr *e = new_expr(E_IDENT, t->line);  /* variable (or a bare payload-less enum variant) */
        e->sval = t->text;
        e->pkg  = g_cur_pkg_prefix;            /* lets a package-local bare variant resolve */
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
                if (hi) { sl->args = (Expr **)realloc(sl->args, sizeof(Expr *)); sl->args[0] = hi; sl->nargs = 1; }
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
                    /* `pkg.name(args)` — a qualified call. hier has no methods, so a
                     * field followed by `(` on a bare identifier is always a package
                     * call; the qualifier resolves to a package prefix in the resolver. */
                    ps->p++;
                    Expr *c = new_expr(E_CALL, t->line);
                    c->sval = f->text;
                    c->qual = e->sval;            /* the qualifier ident, e.g. "geom" */
                    c->pkg  = g_cur_pkg_prefix;
                    int cap = 0;
                    while (!at(ps, TK_RPAREN)) {
                        if (c->nargs == cap) { cap = cap ? cap * 2 : 4; c->args = (Expr **)realloc(c->args, (size_t)cap * sizeof(Expr *)); }
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

static Expr *parse_unary(Parser *ps) {
    if (at(ps, TK_MINUS)) {                /* unary negation: operand in lhs, rhs NULL */
        Tok *t = cur(ps); ps->p++;
        Expr *e = new_expr(E_BINOP, t->line);
        e->op = TK_MINUS; e->lhs = parse_unary(ps);
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

static Expr *parse_cmp(Parser *ps) {            /* comparison level */
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

static Stmt *parse_stmt(Parser *ps) {
    Tok *t = cur(ps);

    if (t->kind == TK_RETURN) {
        ps->p++;
        Stmt *s = new_stmt(S_RETURN, t->line);
        if (!at(ps, TK_NEWLINE)) {
            Expr *first = parse_expr(ps);
            if (at(ps, TK_COMMA)) {       /* return a, b, ... builds a tuple */
                Expr *e = new_expr(E_TUPLE, t->line);
                int cap = 4; e->args = (Expr **)realloc(e->args, (size_t)cap * sizeof(Expr *));
                e->args[e->nargs++] = first;
                while (accept(ps, TK_COMMA)) {
                    if (e->nargs == cap) { cap *= 2; e->args = (Expr **)realloc(e->args, (size_t)cap * sizeof(Expr *)); }
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
    if (t->kind == TK_MATCH) {
        ps->p++;
        Stmt *s = new_stmt(S_MATCH, t->line);
        s->expr = parse_expr(ps);                 /* the Option/enum being matched */
        eat(ps, TK_COLON, "':' before the match arms");
        eat(ps, TK_NEWLINE, "newline");
        eat(ps, TK_INDENT, "indented match arms");
        int cap = 0;
        /* each arm: `Variant(b0, b1, ...):` or `Variant:`, then a block.
         * Exhaustiveness/arity are checked against the scrutinee in the resolver. */
        while (!at(ps, TK_DEDENT) && !at(ps, TK_EOF)) {
            if (accept(ps, TK_NEWLINE)) continue;
            Tok *vn = eat(ps, TK_IDENT, "a match arm `Variant(bindings):` or `Variant:`");
            const char *vqual = NULL, *vname = vn->text;
            if (accept(ps, TK_DOT)) {           /* qualified `pkg.Variant:` */
                vqual = vn->text;
                vname = eat(ps, TK_IDENT, "a variant name after the package qualifier")->text;
            }
            if (s->narms == cap) { cap = cap ? cap * 2 : 4; s->arms = (MatchArm *)realloc(s->arms, (size_t)cap * sizeof(MatchArm)); }
            MatchArm *arm = &s->arms[s->narms++];
            /* Option/Result arms (Some/None/Ok/Err) are never package symbols; an
             * enum variant is package-scoped, mangled with the qualifier's package
             * (or, unqualified, the package this match is parsed in). */
            if (vqual)
                arm->variant = sfmt("%s%s", pkg_prefix_for(vqual), vname);
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
            arm->body = parse_block(ps, &arm->nbody);
        }
        eat(ps, TK_DEDENT, "end of the match arms");
        if (s->narms == 0) die_at(t->line, "match needs at least one arm");
        return s;
    }
    if (t->kind == TK_IF) {
        ps->p++;
        return parse_if(ps, t->line);
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
        if (e->kind != E_INDEX && e->kind != E_FIELD && e->kind != E_TUPIDX)
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
    pr->name = pkg_mangle(nameT->text);
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
    sd->name = pkg_mangle(nameT->text);
    sd->nfields = 0;
    sd->line = nameT->line;
    g_nstructs++;   /* register the name BEFORE parsing fields, so a field type
                     * may reference this struct — e.g. a recursive `[Node]`
                     * child list. (Parsing is single-pass and sequential, so a
                     * half-built struct is only visible to its own fields.) */
    while (!at(ps, TK_DEDENT) && !at(ps, TK_EOF)) {
        if (accept(ps, TK_NEWLINE)) continue;
        Tok *fn = eat(ps, TK_IDENT, "a field name");
        eat(ps, TK_COLON, "':' after field name");
        Type ft = parse_type(ps);   /* int, string, a struct, [Struct]/[[T]], Option(T), ... */
        if (sd->nfields >= 64) die_at(fn->line, "too many fields (max 64)");
        sd->fields[sd->nfields].name = fn->text;
        sd->fields[sd->nfields].type = ft;
        sd->nfields++;
        eat(ps, TK_NEWLINE, "newline");
    }
    eat(ps, TK_DEDENT, "dedent");
    if (sd->nfields == 0) die_at(nameT->line, "a struct needs at least one field");
}

static void parse_enum(Parser *ps) {
    eat(ps, TK_ENUM, "'enum'");
    Tok *nameT = eat(ps, TK_IDENT, "an enum name");
    if (struct_find(pkg_mangle(nameT->text)) >= 0 || enum_find(pkg_mangle(nameT->text)) >= 0)
        die_at(nameT->line, "'%s' is already defined", nameT->text);
    if (g_nenums >= 64) die_at(nameT->line, "too many enums");
    eat(ps, TK_COLON, "':' before the variants");
    eat(ps, TK_NEWLINE, "newline");
    eat(ps, TK_INDENT, "an indented variant list");
    EnumDef *ed = &g_enums[g_nenums];
    ed->name = pkg_mangle(nameT->text);
    ed->nvariants = 0;
    ed->line = nameT->line;
    g_nenums++;   /* register early so a variant payload can be this enum (recursion) */
    while (!at(ps, TK_DEDENT) && !at(ps, TK_EOF)) {
        if (accept(ps, TK_NEWLINE)) continue;
        Tok *vn = eat(ps, TK_IDENT, "a variant name");
        if (ed->nvariants >= 64) die_at(vn->line, "too many variants (max 64)");
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
}

/* `type X = int` / `type X = float` — a distinct, zero-cost newtype. */
static void parse_typedecl(Parser *ps) {
    eat(ps, TK_TYPE, "'type'");
    Tok *nameT = eat(ps, TK_IDENT, "a type name");
    if (struct_find(nameT->text) >= 0 || enum_find(nameT->text) >= 0 || newtype_find(nameT->text) >= 0)
        die_at(nameT->line, "'%s' is already defined", nameT->text);
    if (g_nnewtypes >= 128) die_at(nameT->line, "too many newtypes");
    eat(ps, TK_EQ, "'=' in a type declaration");
    Type under = parse_type(ps);
    if (under != T_INT && under != T_FLOAT && under != T_STRING && under != T_BOOL)
        die_at(nameT->line, "a newtype's underlying type must be int, float, string, or bool (got %s)", type_name(under));
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
static Import g_imports[128];
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
    if (g_nimports < 128) {
        g_imports[g_nimports].alias = alias;
        g_imports[g_nimports].path  = path->text;
        g_imports[g_nimports].line  = kw->line;
        g_nimports++;
    }
    accept(ps, TK_NEWLINE);
}

/* Map a source qualifier (`geom` in `geom.add`) to its package prefix. An
 * aliased import (`import g "math/geom"`) binds the alias to the package's real
 * name (the path's last component); a plain `import "geom"` binds the name
 * itself. Unknown qualifiers fall through to `<qualifier>__` and fail loudly at
 * lookup if no such package was imported. */
static char *pkg_prefix_for(const char *qualifier) {
    const char *pkgname = qualifier;
    for (int i = 0; i < g_nimports; i++) {
        if (g_imports[i].alias && !strcmp(g_imports[i].alias, qualifier)) {
            const char *p = g_imports[i].path;
            const char *slash = strrchr(p, '/');
            pkgname = slash ? slash + 1 : p;
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
        const char *p = g_imports[i].path, *slash = strrchr(p, '/');
        if (!strcmp(slash ? slash + 1 : p, name)) return 1;
    }
    return 0;
}

static ProcVec parse_program(Tok *toks) {
    Parser ps = { toks, 0 };
    ProcVec out = {0};
    g_parsed_package = NULL;                     /* reset per file; set if a `package` decl is seen */
    while (!at(&ps, TK_EOF)) {
        if (accept(&ps, TK_NEWLINE)) continue;
        if (at(&ps, TK_IDENT) && !strcmp(cur(&ps)->text, "package")) { parse_package_decl(&ps); continue; }
        if (at(&ps, TK_IDENT) && !strcmp(cur(&ps)->text, "import"))  { parse_import_decl(&ps);  continue; }
        if (at(&ps, TK_STRUCT)) { parse_struct(&ps); continue; }
        if (at(&ps, TK_ENUM))   { parse_enum(&ps); continue; }
        if (at(&ps, TK_TYPE))   { parse_typedecl(&ps); continue; }
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
    Type        params[16];
    int         inout[16];   /* per-param: is it an inout (by-pointer) param? */
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
    g_sigs[g_nsigs++] = (Sig){ .name="read_all",.ret=T_STRING,      .params={ 0 },                       .nparams=0, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="chr",    .ret=T_STRING,       .params={ T_INT },                   .nparams=1, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="die",    .ret=T_VOID,         .params={ T_STRING },                .nparams=1, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="str",    .ret=T_STRING,       .params={ T_INT },                   .nparams=1, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="substr", .ret=T_STRING,       .params={ T_STRING, T_INT, T_INT },  .nparams=3, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="find",   .ret=T_INT,          .params={ T_STRING, T_STRING },      .nparams=2, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="split",  .ret=T_ARRAY_STRING, .params={ T_STRING, T_STRING },      .nparams=2, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="read_file",.ret=T_STRING,     .params={ T_STRING },                .nparams=1, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="list_dir",.ret=T_ARRAY_STRING, .params={ T_STRING },               .nparams=1, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="args",   .ret=T_ARRAY_STRING, .params={ 0 },                       .nparams=0, .builtin=1 };
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

static Type resolve_exp(Expr *e, Type want);   /* defined below; fixes a None's type */
static Type g_fn_ret = T_VOID;   /* return type of the proc currently being resolved (for or_return) */

/* A place we can mutate in place (take `&` of in C): a variable, a field of
 * such a place, or an element of a composite array (a projection). For an
 * ARRC element, gen_lvalue yields a pointer into the backing buffer
 * (hier_arr_C<id>_ptr), so `arr[i].f = v` and `push(arr[i].xs, v)` mutate the
 * element in place without exposing a pointer to Hier. A scalar-array or
 * string index is not a mutable interior, so it is never an inner lvalue. */
static int is_lvalue(Expr *e) {
    if (e->kind == E_IDENT) return 1;
    if (e->kind == E_FIELD) return is_lvalue(e->lhs);
    if (e->kind == E_TUPIDX) return is_lvalue(e->lhs);   /* t.0 = v: a tuple element is a place */
    if (e->kind == E_INDEX) return (IS_ARRC(e->lhs->type) || IS_SOA(e->lhs->type)) && is_lvalue(e->lhs);
    return 0;
}

static Type resolve_expr(Expr *e) {
    switch (e->kind) {
        case E_INT:  return e->type = T_INT;
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
        case E_STR:  return e->type = T_STRING;
        case E_IDENT: {
            Type t;
            if (vars_find(e->sval, &t)) return e->type = t;
            int evi, eid = variant_find(e->sval, &evi);   /* a payload-less enum variant? */
            if (eid < 0 && e->pkg && e->pkg[0])           /* try this package's prefixed variant */
                eid = variant_find(sfmt("%s%s", e->pkg, e->sval), &evi);
            if (eid >= 0) {
                if (g_enums[eid].variants[evi].npayload != 0)
                    die_at(e->line, "%s carries a payload — write %s(...)", e->sval, e->sval);
                e->kind = E_CALL; e->op = TK_ENUM; e->ival = evi; e->nargs = 0;   /* 0-arg constructor */
                return e->type = ENUM_TYPE(eid);
            }
            die_at(e->line, "unknown variable '%s'", e->sval);
        }
        case E_ARRLIT: {
            if (e->op == TK_COLON) {           /* map literal ["k": v, ...] */
                if (e->nargs == 0)             /* empty []string: V — type carried in ival */
                    return e->type = (Type)e->ival;
                /* args interleave k0,v0,k1,v1,...; keys string, values all int
                 * or all float (the value type picks [string: int]/[string: float]). */
                Type vt = resolve_expr(e->args[1]);
                if (vt != T_INT && vt != T_FLOAT)
                    die_at(e->line, "map values must be int or float");
                for (int i = 0; i < e->nargs; i += 2) {
                    if (resolve_expr(e->args[i]) != T_STRING)
                        die_at(e->line, "map keys must be string");
                    if (resolve_expr(e->args[i + 1]) != vt)
                        die_at(e->line, "map values must all have the same type");
                }
                return e->type = map_of(vt);
            }
            if (e->nargs == 0)                 /* empty literal: type from []T / []K: V */
                return e->type = (Type)e->ival;
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
            Type bt = resolve_expr(e->lhs);
            if (resolve_expr(e->rhs) != T_INT)
                die_at(e->line, "index must be int");
            if (is_array(bt)) return e->type = arr_elem(bt);   /* array element */
            if (IS_SOA(bt)) return e->type = soa_struct(bt);   /* soa element (only valid under .field) */
            if (bt == T_STRING) return e->type = T_INT;        /* string byte */
            die_at(e->line, "can only index an array or a string");
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
                char *q = sfmt("%s%s", pkg_prefix_for(e->lhs->sval), e->sval);
                int evi, eid = variant_find(q, &evi);
                if (eid < 0)
                    die_at(e->line, "package '%s' has no variant '%s'", e->lhs->sval, e->sval);
                if (g_enums[eid].variants[evi].npayload != 0)
                    die_at(e->line, "%s.%s carries a payload — write %s.%s(...)",
                           e->lhs->sval, e->sval, e->lhs->sval, e->sval);
                e->kind = E_CALL; e->sval = q; e->op = TK_ENUM; e->ival = evi; e->nargs = 0;
                return e->type = ENUM_TYPE(eid);
            }
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
            /* Package resolution (Stage B): rewrite e->sval to the package-mangled
             * name before any lookup. An explicit `pkg.name` (e->qual) MUST resolve
             * in that package; an implicit name in an imported package (e->pkg) tries
             * its own package first, else falls through to builtins/unprefixed. */
            if (e->qual) {
                int _vi;
                char *q = sfmt("%s%s", pkg_prefix_for(e->qual), e->sval);
                if (sig_find(q) || struct_find(q) >= 0 || newtype_find(q) >= 0 || variant_find(q, &_vi) >= 0)
                    e->sval = q;
                else
                    die_at(e->line, "package '%s' has no symbol '%s'", e->qual, e->sval);
            } else if (e->pkg && e->pkg[0]) {
                int _vi;
                char *q = sfmt("%s%s", e->pkg, e->sval);
                if (sig_find(q) || struct_find(q) >= 0 || newtype_find(q) >= 0 || variant_find(q, &_vi) >= 0)
                    e->sval = q;
            }
            /* a call whose name is a newtype wraps its underlying value: Meters(x)
             * with x : float -> Meters. Zero-cost; codegen is the identity. */
            int ntid = newtype_find(e->sval);
            if (ntid >= 0) {
                Type under = g_newtypes[ntid].under;
                if (e->nargs != 1)
                    die_at(e->line, "%s(x) takes one %s", e->sval, type_name(under));
                Type at_ = resolve_expr(e->args[0]);
                if (at_ != under)
                    die_at(e->line, "%s(x) needs a %s, got %s", e->sval, type_name(under), type_name(at_));
                e->op = TK_TYPE;   /* mark as a newtype wrap for codegen (identity) */
                return e->type = NT_TYPE(ntid);
            }
            /* a call whose name is a struct is positional construction */
            int sid = struct_find(e->sval);
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
            /* str is polymorphic (int or float); to_int/to_float convert
             * between the two (no implicit mixing exists). Handled inline so
             * they bypass the fixed-signature Sig table. */
            if (!strcmp(e->sval, "str")) {
                if (e->nargs != 1) die_at(e->line, "str(x) takes one argument");
                Type b = base_of(resolve_expr(e->args[0]));   /* sees through a newtype */
                if (b != T_INT && b != T_FLOAT)
                    die_at(e->line, "str(x) takes an int or a float");
                return e->type = T_STRING;
            }
            if (!strcmp(e->sval, "to_float")) {   /* int -> float, or unwrap a float newtype */
                if (e->nargs != 1) die_at(e->line, "to_float(n) takes one argument");
                Type at_ = resolve_expr(e->args[0]);
                if (at_ != T_INT && !(IS_NEWTYPE(at_) && nt_under(at_) == T_FLOAT))
                    die_at(e->line, "to_float(n) takes an int or a float newtype");
                return e->type = T_FLOAT;
            }
            if (!strcmp(e->sval, "to_int")) {   /* float -> int (truncate), or unwrap an int newtype */
                if (e->nargs != 1) die_at(e->line, "to_int(x) takes one argument");
                Type at_ = resolve_expr(e->args[0]);
                if (at_ != T_FLOAT && !(IS_NEWTYPE(at_) && nt_under(at_) == T_INT))
                    die_at(e->line, "to_int(x) takes a float (truncates toward zero) or an int newtype");
                return e->type = T_INT;
            }
            if (!strcmp(e->sval, "to_str")) {   /* unwrap a string newtype -> string */
                if (e->nargs != 1) die_at(e->line, "to_str(x) takes one argument");
                Type at_ = resolve_expr(e->args[0]);
                if (!(IS_NEWTYPE(at_) && nt_under(at_) == T_STRING))
                    die_at(e->line, "to_str(x) takes a string newtype");
                return e->type = T_STRING;
            }
            if (!strcmp(e->sval, "to_bool")) {   /* unwrap a bool newtype -> bool */
                if (e->nargs != 1) die_at(e->line, "to_bool(x) takes one argument");
                Type at_ = resolve_expr(e->args[0]);
                if (!(IS_NEWTYPE(at_) && nt_under(at_) == T_BOOL))
                    die_at(e->line, "to_bool(x) takes a bool newtype");
                return e->type = T_BOOL;
            }
            /* array builtins (don't fit the scalar Sig table) */
            if (!strcmp(e->sval, "len")) {
                if (e->nargs != 1) die_at(e->line, "len(...) takes one argument");
                Type at_ = resolve_expr(e->args[0]);
                if (!is_array(at_) && at_ != T_STRING && !is_map(at_) && !IS_SOA(at_))
                    die_at(e->line, "len(...) takes an array, a string, a map, or a soa");
                return e->type = T_INT;
            }
            /* map builtins ([string: int] or [string: float]). The value type
             * follows the map. map_set is pure (returns a new map); the
             * m = map_set(m, ...) self-rebind is grown in place by the
             * accumulator pass, like array push / string append. */
            if (!strcmp(e->sval, "map_set")) {
                if (e->nargs != 3) die_at(e->line, "map_set(m, key, value) takes three arguments");
                Type mt = resolve_expr(e->args[0]);
                if (!is_map(mt)) die_at(e->line, "map_set's first argument must be a map");
                if (resolve_expr(e->args[1]) != T_STRING) die_at(e->line, "map_set key must be string");
                if (resolve_expr(e->args[2]) != map_val(mt))
                    die_at(e->line, "map_set value must be %s", type_name(map_val(mt)));
                return e->type = mt;
            }
            if (!strcmp(e->sval, "map_get")) {
                if (e->nargs != 3) die_at(e->line, "map_get(m, key, default) takes three arguments");
                Type mt = resolve_expr(e->args[0]);
                if (!is_map(mt)) die_at(e->line, "map_get's first argument must be a map");
                if (resolve_expr(e->args[1]) != T_STRING) die_at(e->line, "map_get key must be string");
                if (resolve_expr(e->args[2]) != map_val(mt))
                    die_at(e->line, "map_get default must be %s", type_name(map_val(mt)));
                return e->type = map_val(mt);
            }
            if (!strcmp(e->sval, "map_has")) {
                if (e->nargs != 2) die_at(e->line, "map_has(m, key) takes two arguments");
                if (!is_map(resolve_expr(e->args[0])))
                    die_at(e->line, "map_has's first argument must be a map");
                if (resolve_expr(e->args[1]) != T_STRING) die_at(e->line, "map_has key must be string");
                return e->type = T_BOOL;
            }
            /* map_del is pure (returns a new map); the m = map_del(m, k)
             * self-rebind is rewritten to an in-place tombstone delete. */
            if (!strcmp(e->sval, "map_del")) {
                if (e->nargs != 2) die_at(e->line, "map_del(m, key) takes two arguments");
                Type mt = resolve_expr(e->args[0]);
                if (!is_map(mt)) die_at(e->line, "map_del's first argument must be a map");
                if (resolve_expr(e->args[1]) != T_STRING) die_at(e->line, "map_del key must be string");
                return e->type = mt;
            }
            /* keys(m) -> [string]: the map's live keys, for iteration. */
            if (!strcmp(e->sval, "keys")) {
                if (e->nargs != 1) die_at(e->line, "keys(m) takes one argument");
                if (!is_map(resolve_expr(e->args[0])))
                    die_at(e->line, "keys's argument must be a map");
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
                if (!is_array(arrt) && !IS_SOA(arrt))
                    die_at(e->line, "push's first argument must be an array or soa");
                if (!is_lvalue(e->args[0]))
                    die_at(e->line, "cannot push through this expression — the array must be a "
                                    "variable, field, or composite-array element");
                /* push through a heap inout array is allowed: the regrow
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
            Sig *s = sig_find(e->sval);
            if (!s) die_at(e->line, "unknown procedure '%s'", e->sval);
            if (e->nargs != s->nparams)
                die_at(e->line, "'%s' takes %d argument(s), got %d",
                       e->sval, s->nparams, e->nargs);
            for (int i = 0; i < e->nargs; i++) {
                Type at_ = resolve_exp(e->args[i], s->params[i]);   /* fixes a None arg */
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
            /* A slice argument views its source's buffer; an inout of that same
             * variable in the same call could reallocate the buffer (e.g. push),
             * leaving the slice dangling. Forbid the overlap. */
            for (int i = 0; i < e->nargs; i++) {
                if (e->args[i]->kind != E_SLICE) continue;
                Expr *si = e->args[i]->lhs;
                while (si->kind == E_FIELD || si->kind == E_INDEX || si->kind == E_SLICE) si = si->lhs;
                if (si->kind != E_IDENT) continue;
                for (int j = 0; j < e->nargs; j++) {
                    if (!s->inout[j]) continue;
                    Expr *rj = e->args[j]->lhs;
                    while (rj->kind == E_FIELD || rj->kind == E_INDEX) rj = rj->lhs;
                    if (rj->kind == E_IDENT && !strcmp(rj->sval, si->sval))
                        die_at(e->line, "cannot pass a slice of '%s' and an inout of '%s' in one call "
                               "(the inout may reallocate the buffer the slice views)", si->sval, si->sval);
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
            Type lt = resolve_expr(e->lhs);
            Type rt = resolve_expr(e->rhs);
            if (e->op == TK_AND || e->op == TK_OR) {
                if (lt != T_BOOL || rt != T_BOOL)
                    die_at(e->line, "`%s` needs bool operands", e->op == TK_AND ? "and" : "or");
                return e->type = T_BOOL;
            }
            if (is_cmp(e->op)) {
                if (e->op == TK_EQEQ || e->op == TK_NEQ) {
                    /* equality is structural for every type (value semantics):
                     * ints/bools directly, strings/arrays/structs by content,
                     * recursing through nesting. Only void is incomparable. */
                    if (lt != rt)
                        die_at(e->line, "cannot compare %s with %s", type_name(lt), type_name(rt));
                    if (lt == T_VOID) die_at(e->line, "cannot compare void");
                    if (IS_OPT(lt)) die_at(e->line, "cannot compare Option values; match on them instead");
                    if (IS_RES(lt)) die_at(e->line, "cannot compare Result values; match on them instead");
                } else {
                    /* ordering: two ints, two floats, two strings, or two values
                     * of the SAME numeric/string newtype */
                    Type b = base_of(lt);
                    int ok = lt == rt && (b == T_INT || b == T_CHAR || b == T_FLOAT || b == T_STRING);
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
            /* arithmetic: two ints or two floats (no implicit mixing — use
             * to_float/to_int). float `/` is true division; int `/` truncates.
             * Two values of the SAME numeric newtype yield that newtype, so units
             * stay typed (Meters + Meters -> Meters) and can't mix with the base. */
            if (lt == T_INT && rt == T_INT) return e->type = T_INT;
            /* char arithmetic stays in the byte domain: char±int, int±char, char±char
             * -> char (so `'0' + d` is a char, ready for an in-place string append). */
            if ((e->op == TK_PLUS || e->op == TK_MINUS) &&
                (lt == T_CHAR || rt == T_CHAR) &&
                (lt == T_CHAR || lt == T_INT) && (rt == T_CHAR || rt == T_INT))
                return e->type = T_CHAR;
            if (lt == T_FLOAT && rt == T_FLOAT) return e->type = T_FLOAT;
            if (lt == rt && IS_NEWTYPE(lt) && (nt_under(lt) == T_INT || nt_under(lt) == T_FLOAT))
                return e->type = lt;
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
 * back onto the E_NONE node so codegen emits the right HierOpt. */
static Type resolve_exp(Expr *e, Type want) {
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

static void resolve_stmt(Stmt *s, Type ret) {
    switch (s->kind) {
        case S_DECL: {
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
                if (!vars_find(s->names[i], &vt))
                    die_at(s->line, "assignment to unknown variable '%s' (use ':=' to declare)", s->names[i]);
                s->mtypes[i] = tup_elem(rt, i);
                if (s->mtypes[i] != vt)
                    die_at(s->line, "cannot assign %s to '%s' of type %s",
                           type_name(s->mtypes[i]), s->names[i], type_name(vt));
            }
            break;
        }
        case S_ASSIGN: {
            Type vt;
            if (!vars_find(s->name, &vt))
                die_at(s->line, "assignment to unknown variable '%s'", s->name);
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
        case S_MATCH: {
            Type st = resolve_expr(s->expr);
            if (IS_OPT(st)) {
                Type inner = opt_inner(st);
                int some = 0, none = 0;
                for (int i = 0; i < s->narms; i++) {
                    MatchArm *arm = &s->arms[i];
                    int m = vars_mark();
                    if (!strcmp(arm->variant, "Some")) {
                        if (some) die_at(arm->line, "duplicate Some arm");
                        if (arm->nbinds != 1) die_at(arm->line, "Some(x) binds exactly one value");
                        vars_push(arm->binds[0], inner, 1); some = 1;
                    } else if (!strcmp(arm->variant, "None")) {
                        if (none) die_at(arm->line, "duplicate None arm");
                        if (arm->nbinds != 0) die_at(arm->line, "None binds nothing");
                        none = 1;
                    } else {
                        die_at(arm->line, "an Option's arms are Some(x) and None, not '%s'", arm->variant);
                    }
                    resolve_block(arm->body, arm->nbody, ret);
                    vars_restore(m);
                }
                if (!some || !none) die_at(s->line, "match on an Option must cover both Some and None");
            } else if (IS_RES(st)) {
                Type okt = res_ok(st), errt = res_err(st);
                int ok = 0, err = 0;
                for (int i = 0; i < s->narms; i++) {
                    MatchArm *arm = &s->arms[i];
                    int m = vars_mark();
                    if (!strcmp(arm->variant, "Ok")) {
                        if (ok) die_at(arm->line, "duplicate Ok arm");
                        if (arm->nbinds != 1) die_at(arm->line, "Ok(x) binds exactly one value");
                        vars_push(arm->binds[0], okt, 1); ok = 1;
                    } else if (!strcmp(arm->variant, "Err")) {
                        if (err) die_at(arm->line, "duplicate Err arm");
                        if (arm->nbinds != 1) die_at(arm->line, "Err(e) binds exactly one value");
                        vars_push(arm->binds[0], errt, 1); err = 1;
                    } else {
                        die_at(arm->line, "a Result's arms are Ok(x) and Err(e), not '%s'", arm->variant);
                    }
                    resolve_block(arm->body, arm->nbody, ret);
                    vars_restore(m);
                }
                if (!ok || !err) die_at(s->line, "match on a Result must cover both Ok and Err");
            } else if (IS_ENUM(st)) {
                EnumDef *ed = &g_enums[ENUM_ID(st)];
                int covered[64] = {0};
                for (int i = 0; i < s->narms; i++) {
                    MatchArm *arm = &s->arms[i];
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
                for (int v = 0; v < ed->nvariants; v++)
                    if (!covered[v])
                        die_at(s->line, "non-exhaustive match: missing variant %s of %s",
                               ed->variants[v].name, ed->name);
            } else {
                die_at(s->line, "match expects an Option, Result, or enum value, got %s", type_name(st));
            }
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
            if (!is_array(arrt))
                die_at(s->line, "can only index-assign an array element (strings themselves are immutable)");
            if (!is_lvalue(s->target->lhs))
                die_at(s->line, "cannot index-assign through this expression (only a variable, field, or composite-array element is a place)");
            Type tt = resolve_expr(s->target);    /* E_INDEX -> element type */
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
            Type tt = resolve_expr(s->target);   /* E_FIELD -> field type; also types the chain for is_lvalue */
            if (!is_lvalue(s->target))
                die_at(s->line, "cannot assign to a field of a temporary (only variables, fields, and composite-array elements are places)");
            Type vt = resolve_exp(s->expr, tt);  /* coerces a None value */
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
        if (g_nsigs >= 256) die_at(pr->line, "too many functions (max 256 including builtins)");
        Sig s; memset(&s, 0, sizeof s);
        s.name = pr->name; s.ret = pr->ret; s.nparams = pr->nparams; s.builtin = 0;
        if (pr->nparams > 16) die_at(pr->line, "too many parameters (max 16)");
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
            /* arrays, maps, and soa are read-only borrows (they shallow-share
             * the caller's buffers, so in-place mutation would reach through),
             * EXCEPT an inout one, which is a by-pointer share the callee may
             * mutate in place. To mutate a borrowed container, copy it first
             * (`local := param`). */
            int mutable = (!is_array(pt) && !is_map(pt) && !IS_SOA(pt))
                          || pr->params[j].is_inout;
            vars_push(pr->params[j].name, pt, mutable);
        }
        if (!strcmp(pr->name, "main") && (pr->nparams != 0 || pr->ret != T_VOID))
            die_at(pr->line, "'main' must be 'fn main():' with no return");
        g_fn_ret = pr->ret;
        resolve_block(pr->body, pr->nbody, pr->ret);
    }
}

/* ------------------------------------------------------------- codegen */
/* --- bounds-check elision for monotone loop indices -----------------------
 * Inside `for i in range(len(A)):` (start 0, step +1), the access `A[i]` is
 * provably in [0, len(A)): the C loop caches `_stop = len(A)` once at entry,
 * `len` is an un-redefinable builtin returning the true length, Hier has NO
 * in-place array-shrink op, and we verify below that the loop body never
 * reassigns/shadows A or i and never passes A whole to a call (push / a
 * possibly-inout callee could change it). So the per-element bounds check is
 * redundant and we emit the raw `A.data[i]`. A read `A[i]` and `print(A[i])`,
 * `acc = acc + A[i]` stay elidable (they pass `A[i]`, not `A`); `A = ...`,
 * `push(A, x)`, `f(A)` all disable it. Escape hatch: HIERC_NO_BOUNDS_ELISION=1.
 * This is provably-safe range narrowing, NOT a blanket "trust the index". */
typedef struct { const char *iv, *arr; } ElidePair;
static ElidePair g_elide[64];   /* active (loopvar,array) pairs, one per enclosing safe loop */
static int g_nelide;
static int g_elide_disabled = -1;
static int elision_on(void) {
    if (g_elide_disabled < 0) g_elide_disabled = getenv("HIERC_NO_BOUNDS_ELISION") ? 1 : 0;
    return !g_elide_disabled;
}

/* Does `e` pass the whole array `arr` (a bare identifier) as a direct argument
 * to any call? Such a call may be inout and shrink/rebind it -> not elidable. */
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
static int g_nparam = 0;
static int is_param(const char *name) {
    for (int i = 0; i < g_nparam; i++)
        if (!strcmp(g_param[i], name)) return 1;
    return 0;
}

/* HEAP inout params additionally carry their value's owning arena as a hidden
 * C parameter `_ina_<name>`. Any allocating mutation of the param (a [string]
 * element copy, a heap struct field copy, an array regrow/push) must allocate
 * into THAT arena — the caller's, where the value lives — not the callee's
 * _scope. Non-heap inout (int/bool/pure struct) never allocates, so it has no
 * arena param. Populated per proc alongside g_inout. */
static const char *g_heap_inout[16];
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
    }
    return c;
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
 * `&`-inout): that write would reach through into the scrutinee and break
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
    if (e->kind == E_ADDR) {                  /* &nm... — an inout argument */
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
    if (IS_NEWTYPE(t)) t = nt_under(t);   /* re-home as its base (int/float: nothing to do) */
    switch (t) {
        case T_STRING:       return sfmt("hier_str_copy(%s, %s)", arena, val);
        case T_ARRAY_INT:    return sfmt("hier_arr_int_copy(%s, %s)", arena, val);
        case T_ARRAY_FLOAT:  return sfmt("hier_arr_float_copy(%s, %s)", arena, val);
        case T_ARRAY_STRING: return sfmt("hier_arr_str_copy(%s, %s)", arena, val);
        case T_MAP_SI:
        case T_MAP_SF:       return sfmt("hier_map_%s_copy(%s, %s)", map_fn(t), arena, val);
        default:
            if (IS_OPT(t))
                return type_is_heap(t) ? sfmt("hier_opt%d_copy(%s, %s)", OPT_ID(t), arena, val) : val;
            if (IS_RES(t))
                return type_is_heap(t) ? sfmt("hier_res%d_copy(%s, %s)", RES_ID(t), arena, val) : val;
            if (IS_TUP(t))
                return type_is_heap(t) ? sfmt("hier_tup%d_copy(%s, %s)", TUP_ID(t), arena, val) : val;
            if (IS_ENUM(t))
                return type_is_heap(t) ? sfmt("hier_copy_E_%s(%s, %s)", g_enums[ENUM_ID(t)].name, arena, val) : val;
            if (IS_ARRC(t))
                return sfmt("hier_arr_C%d_copy(%s, %s)", ARRC_ID(t), arena, val);
            if (IS_STRUCT(t) && type_is_heap(t))
                return sfmt("hier_copy_S_%s(%s, %s)", g_structs[STRUCT_ID(t)].name, arena, val);
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
 * self-hosting hierc0, whose field_type/sig_ret/resolve_nt return Ctx fields). */
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

/* `t = C(..., t, ...)` — a self-rebuild of a heap aggregate. The old t is read
 * once in the RHS and immediately replaced, so handing off its buffer (rather
 * than deep-copying it) is sound even in a loop. Gate: the target is a tracked
 * same-arena local (not a borrowed/inout param), the RHS is a heap value, and
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
 * byte; arrays element-wise; structs field-wise via a generated hier_eq_S_X.
 * Recurses through nesting exactly as the deep copy does. */
static char *gen_eq(Type t, const char *a, const char *b) {
    if (IS_NEWTYPE(t))       return gen_eq(nt_under(t), a, b);
    if (t == T_STRING)       return sfmt("(strcmp(%s, %s) == 0)", a, b);
    if (t == T_ARRAY_INT)    return sfmt("hier_arr_int_eq(%s, %s)", a, b);
    if (t == T_ARRAY_FLOAT)  return sfmt("hier_arr_float_eq(%s, %s)", a, b);
    if (t == T_ARRAY_STRING) return sfmt("hier_arr_str_eq(%s, %s)", a, b);
    if (is_map(t))           return sfmt("hier_map_%s_eq(%s, %s)", map_fn(t), a, b);
    if (IS_ARRC(t))          return sfmt("hier_arr_C%d_eq(%s, %s)", ARRC_ID(t), a, b);
    if (IS_ENUM(t))          return sfmt("hier_eq_E_%s(%s, %s)", g_enums[ENUM_ID(t)].name, a, b);
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
    if (IS_STRUCT(t))        return sfmt("hier_eq_S_%s(%s, %s)", g_structs[STRUCT_ID(t)].name, a, b);
    if (IS_SOA(t))           return sfmt("Soa%d_eq(%s, %s)", SOA_ID(t), a, b);
    return sfmt("(%s == %s)", a, b);   /* int/bool/float */
}

static char *gen_call(Expr *e, const char *arena) {
    if (e->op == TK_TYPE)     /* newtype wrap Meters(x): zero-cost, just the value */
        return gen_expr(e->args[0], arena);
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
        return sfmt("hier_map_%s_set(%s, %s, %s, %s)", map_fn(e->type), arena, m, k, v);
    }
    if (!strcmp(e->sval, "map_get")) {
        char *m = gen_expr(e->args[0], arena);
        char *k = gen_expr(e->args[1], arena);
        char *d = gen_expr(e->args[2], arena);
        return sfmt("hier_map_%s_get(%s, %s, %s)", map_fn(e->args[0]->type), m, k, d);
    }
    if (!strcmp(e->sval, "map_has")) {
        char *m = gen_expr(e->args[0], arena);
        char *k = gen_expr(e->args[1], arena);
        return sfmt("hier_map_%s_has(%s, %s)", map_fn(e->args[0]->type), m, k);
    }
    /* map_del pure: deep-copy + delete into `arena`; the accumulator pass
     * rewrites a self-rebind to an in-place tombstone delete separately. */
    if (!strcmp(e->sval, "map_del")) {
        char *m = gen_expr(e->args[0], arena);
        char *k = gen_expr(e->args[1], arena);
        return sfmt("hier_map_%s_del_pure(%s, %s, %s)", map_fn(e->type), arena, m, k);
    }
    if (!strcmp(e->sval, "keys")) {
        char *m = gen_expr(e->args[0], arena);
        return sfmt("hier_map_%s_keys(%s, %s)", map_fn(e->args[0]->type), arena, m);
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
        char *arr = gen_lvalue(e->args[0], arena);   /* lvalue so a projected `arr[i].xs` is the buffer slot */
        char *v = gen_expr(e->args[1], arena);
        if (IS_SOA(e->args[0]->type))   /* struct-of-arrays push: grow each field buffer + scatter */
            return sfmt("Soa%d_push(%s, &(%s), %s)", SOA_ID(e->args[0]->type), owner, arr, v);
        /* push has the same (owner, &arr, v) shape for every element type */
        return sfmt("hier_arr_%s_push(%s, &(%s), %s)", arr_fn(e->args[0]->type), owner, arr, v);
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
    if (!strcmp(e->sval, "read_all")) {
        return sfmt("hier_read_all(%s)", arena);
    }
    if (!strcmp(e->sval, "read_file")) {
        return sfmt("hier_read_file(%s, %s)", arena, gen_expr(e->args[0], arena));
    }
    if (!strcmp(e->sval, "list_dir")) {
        return sfmt("hier_list_dir(%s, %s)", arena, gen_expr(e->args[0], arena));
    }
    if (!strcmp(e->sval, "args")) {
        return sfmt("hier_args(%s)", arena);
    }
    if (!strcmp(e->sval, "chr")) {
        return sfmt("hier_chr(%s, %s)", arena, gen_expr(e->args[0], arena));
    }
    if (!strcmp(e->sval, "die")) {   /* print to stderr and exit(1); never returns */
        return sfmt("hier_die(%s)", gen_expr(e->args[0], arena));
    }
    if (!strcmp(e->sval, "str")) {
        char *a = gen_expr(e->args[0], arena);
        if (base_of(e->args[0]->type) == T_FLOAT)   /* sees through a float newtype */
            return sfmt("hier_float_to_str(%s, %s)", arena, a);
        return sfmt("hier_int_to_str(%s, %s)", arena, a);
    }
    if (!strcmp(e->sval, "to_float"))    /* int -> double */
        return sfmt("((double)%s)", gen_expr(e->args[0], arena));
    if (!strcmp(e->sval, "to_int"))      /* double -> long, truncates toward zero */
        return sfmt("((long)%s)", gen_expr(e->args[0], arena));
    if (!strcmp(e->sval, "to_str") || !strcmp(e->sval, "to_bool"))   /* zero-cost newtype unwrap */
        return gen_expr(e->args[0], arena);
    /* user proc: first arg is the destination arena for its return. A heap
     * inout parameter takes TWO C args: the value's owning arena, then the
     * &pointer — so an allocating mutation in the callee lands where the
     * value lives. The owner is computed from the argument's root variable
     * (which, if it's itself a heap inout param here, yields its carried
     * _ina_ arena — threading the real owner across recursion). */
    Sig *cs = sig_find(e->sval);
    char *out = sfmt("h_%s(%s", e->sval, arena);
    for (int i = 0; i < e->nargs; i++) {
        /* arguments are transients (the callee's return value is independently
         * owned in _parent — never an alias of an arg), so build them in the
         * current scope, not the result arena which may be an outer scope. */
        char *a = gen_expr(e->args[i], g_cur_scope);
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
        case TK_AND:   return "&&";
        case TK_OR:    return "||";
        default:       return "?";
    }
}

static char *gen_expr(Expr *e, const char *arena) {
    switch (e->kind) {
        case E_INT:  return sfmt("%ldL", e->ival);
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
        case E_STR:  /* a length-headered, interned-once copy (cached per occurrence) */
            return sfmt("({ static char *_l = 0; if (!_l) _l = hier_str_intern(\"%s\"); _l; })", e->sval);
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
        case E_IDENT:return is_inout_param(e->sval) ? sfmt("(*h_%s)", e->sval)
                                                    : sfmt("h_%s", e->sval);
        case E_ADDR: /* &place as an inout arg: address of the underlying
                      * lvalue. gen_lvalue derefs an inout root and projects an
                      * array element to its buffer slot, so `&arr[i].x` is a
                      * real address, not the address of a `_get` temporary. */
            return sfmt("&(%s)", gen_lvalue(e->lhs, arena));
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
            char *a = gen_expr(e->lhs, arena);
            char *ix = gen_expr(e->rhs, arena);
            if (e->lhs->type == T_STRING)
                return sfmt("hier_str_get(%s, %s)", a, ix);   /* O(1): length header, no strlen */
            if (index_in_range(e->lhs, e->rhs))               /* monotone loop index: skip the bounds check */
                return sfmt("(%s).data[%s]", a, ix);
            return sfmt("hier_arr_%s_get(%s, %s)", arr_fn(e->lhs->type), a, ix);
        }
        case E_SLICE: {
            if (e->lhs->type == T_STRING) {   /* s[a:b] -> a fresh substring (substr) */
                int id = g_blk++;
                char *s  = gen_expr(e->lhs, arena);
                char *lo = e->rhs ? gen_expr(e->rhs, arena) : sfmt("0L");
                char *hi = e->nargs ? gen_expr(e->args[0], arena) : sfmt("hier_str_len(_ss%d)", id);
                return sfmt("({ const char *_ss%d = %s; hier_str_substr(%s, _ss%d, %s, %s); })",
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
                            "fprintf(stderr, \"hier: slice [%%ld:%%ld] out of bounds (len %%ld)\\n\", _lo%d, _hi%d, _sv%d.len); exit(1); } "
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
                        "fprintf(stderr, \"hier: slice [%%ld:%%ld] out of bounds (len %%ld)\\n\", _lo%d, _hi%d, _sv%d.len); exit(1); } "
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
                const char *mf = map_fn(e->type);
                char *out = sfmt("({ %s_l%d = hier_map_%s_with_cap(%s, 0L);",
                                 c_type(e->type), id, mf, arena);
                for (int i = 0; i + 1 < e->nargs; i += 2)
                    out = sfmt("%s hier_map_%s_put(%s, &_l%d, %s, %s);",
                               out, mf, arena, id, gen_expr(e->args[i], arena),
                               gen_expr(e->args[i + 1], arena));
                return sfmt("%s _l%d; })", out, id);
            }
            /* array literal: build with_cap, then store each element. copy_into
             * deep-copies it into `arena` so the array owns its bytes — a plain
             * assign for int/float, hier_str_copy for string, the element's deep
             * copy for a struct or nested array. */
            Type elem = arr_elem(e->type);
            char *out = sfmt("({ %s_l%d = hier_arr_%s_with_cap(%s, %dL);",
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
            if (e->op == TK_NOT)               /* unary: operand in lhs, rhs NULL */
                return sfmt("(!%s)", gen_expr(e->lhs, arena));
            if (e->op == TK_MINUS && e->rhs == NULL)   /* unary negation */
                return sfmt("(-%s)", gen_expr(e->lhs, arena));
            char *l = gen_expr(e->lhs, arena);
            char *r = gen_expr(e->rhs, arena);
            /* and/or lower to C's short-circuiting && / || via op_str below */
            if (e->op == TK_PLUS && e->lhs->type == T_STRING)
                return e->rhs->type == T_CHAR
                    ? sfmt("hier_str_concat_char(%s, %s, %s)", arena, l, r)
                    : sfmt("hier_str_concat(%s, %s, %s)", arena, l, r);
            /* equality dispatches by type (deep/structural); != negates it */
            if (e->op == TK_EQEQ || e->op == TK_NEQ) {
                char *eq = gen_eq(e->lhs->type, l, r);
                return e->op == TK_EQEQ ? eq : sfmt("(!%s)", eq);
            }
            /* ordering on strings is lexicographic via strcmp */
            if (is_cmp(e->op) && base_of(e->lhs->type) == T_STRING)   /* string or a string newtype */
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
/* A C lvalue for a place expression — the mutable-mutation counterpart of
 * gen_expr. For E_IDENT/E_FIELD it is exactly what gen_expr already produces
 * (a variable, or `(place).f_x`). The difference is E_INDEX: instead of the
 * by-value `_get` copy, a composite-array element yields a pointer into the
 * backing buffer via the bounds-checked hier_arr_C<id>_ptr, dereferenced to a
 * real lvalue. So `arr[i].f = v`, `m[i][j] = v`, `push(arr[i].xs, v)`, and
 * `&arr[i].x` (inout) all mutate the element in place — Hylo-style projection,
 * with no pointer ever surfaced in Hier. Only ARRC bases are projected
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
        if (index_in_range(e->lhs, e->rhs))   /* monotone loop index: project without the bounds check */
            return sfmt("((%s).data[%s])", gen_lvalue(e->lhs, arena), gen_expr(e->rhs, arena));
        return sfmt("(*hier_arr_C%d_ptr(&(%s), %s))",
                    ARRC_ID(e->lhs->type), gen_lvalue(e->lhs, arena),
                    gen_expr(e->rhs, arena));
    }
    return gen_expr(e, arena);
}

static void gen_stmt(FILE *o, Stmt *s, int ind, const char *scope, Type ret) {
    /* call arguments in this statement's expressions are transients owned by
     * the current scope (see g_cur_scope). Set before generating any expr;
     * the current statement's expressions are always emitted before recursing
     * into nested blocks, so a nested gen_stmt re-setting this is harmless. */
    g_cur_scope = scope;
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
             * is already a freshly-owned value built in `owner` — no copy. And a
             * dead same-arena local is MOVED (copy elided): it takes over the
             * source's buffer (see can_move_from). */
            if (is_place(s->expr) && type_is_heap(s->decl_type) && !can_move_from(s->expr, owner))
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
                /* a char piece appends one byte in place (no strlen, no snprintf);
                 * a string piece appends its bytes. Both grow the same buffer. */
                if (s->expr->rhs->type == T_CHAR)
                    fprintf(o, "hier_str_append_char(%s, &h_%s, &_len_h_%s, &_cap_h_%s, %s);\n",
                            owner, s->name, s->name, s->name, e);
                else
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
                fprintf(o, "hier_map_%s_put(%s, %s, %s, %s);\n", map_fn(s->expr->type), mo, mp, k, v);
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
                fprintf(o, "hier_map_%s_del(%s, %s);\n", map_fn(s->expr->type), mp, k);
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
             * reassigned from a FUNCTION CALL (`a = step(a)`). A Hier call returns
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
            int do_recycle = g_loop_depth > 0 && !is_accum(s->name)
                && cv_arena(s->name) && !is_inout_param(s->name)
                && s->expr->kind == E_CALL && s->expr->op != TK_ENUM
                && is_array(s->expr->type)   /* any element type: we recycle the SPINE buffer
                                              * (data[]), which is dead+unique exactly as for a
                                              * scalar array. Heap elements (strings, sub-arrays)
                                              * are separate, also dead, not recycled -> partial
                                              * reclaim; flat structs -> full. Same mechanism the
                                              * generic push-recycle already uses for every type. */
                && count_reads_b(g_proc_body, g_proc_nbody, s->name) >= 2;
            if (do_recycle) {
                /* CRITICAL ORDER: evaluate the RHS into a temp FIRST (the call
                 * still reads a's old buffer to build its result), THEN recycle
                 * the old buffer, THEN assign. Recycling before the call would let
                 * the call's own allocations hand back a's still-live buffer. */
                int tid = g_blk++;
                indent(o, ind); fprintf(o, "%s_rec%d = %s;\n", c_type(s->expr->type), tid, v);
                indent(o, ind); fprintf(o, "if (h_%s.data) arena_recycle(%s, h_%s.data, (size_t)h_%s.cap * sizeof(*h_%s.data));\n",
                                        s->name, owner, s->name, s->name, s->name);
                indent(o, ind); fprintf(o, "h_%s = _rec%d;\n", s->name, tid);
            } else {
                indent(o, ind);
                /* an inout param is a pointer in the body; assign through it */
                if (is_inout_param(s->name))
                    fprintf(o, "(*h_%s) = %s;\n", s->name, v);
                else
                    fprintf(o, "h_%s = %s;\n", s->name, v);
            }
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
            char *arr = gen_lvalue(arrx, scope);   /* lvalue so a nested `m[i][j]=v` indexes m's buffer */
            char *ix  = gen_expr(s->target->rhs, scope);
            char *v   = gen_expr(s->expr, scope);
            indent(o, ind);
            if (arrx->type == T_ARRAY_STRING || IS_ARRC(arrx->type)) {
                /* string/struct/array element: the set deep-copies it into the
                 * array's owning arena — the carried _ina_ arena if the root is
                 * a heap inout param. */
                const char *owner = (root->kind == E_IDENT) ? owner_arena_of(root->sval) : scope;
                fprintf(o, "hier_arr_%s_set(%s, &(%s), %s, %s);\n", arr_fn(arrx->type), owner, arr, ix, v);
            } else if (index_in_range(s->target->lhs, s->target->rhs)) {
                fprintf(o, "(%s).data[%s] = %s;\n", arr, ix, v);   /* monotone loop index: skip the check */
            } else {   /* [int] or [float]: value word, no arena, no byte copy */
                fprintf(o, "hier_arr_%s_set(&(%s), %s, %s);\n", arr_fn(arrx->type), arr, ix, v);
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
             * carried _ina_ arena if the root is a heap inout param. */
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
                if (ret_must_copy(s->expr)) {
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
                if (ret_must_copy(s->expr)) {
                    char *v = gen_expr(s->expr, "&_scope");
                    indent(o, ind); fprintf(o, "{ HierArrInt _ret = hier_arr_int_copy(_parent, %s); %s return _ret; }\n", v, rf);
                } else {
                    char *v = gen_expr(s->expr, "_parent");
                    indent(o, ind); fprintf(o, "{ HierArrInt _ret = %s; %s return _ret; }\n", v, rf);
                }
            } else if (ret == T_ARRAY_FLOAT) {
                /* promote up, exactly like [int] (a buffer of value words). */
                if (ret_must_copy(s->expr)) {
                    char *v = gen_expr(s->expr, "&_scope");
                    indent(o, ind); fprintf(o, "{ HierArrFloat _ret = hier_arr_float_copy(_parent, %s); %s return _ret; }\n", v, rf);
                } else {
                    char *v = gen_expr(s->expr, "_parent");
                    indent(o, ind); fprintf(o, "{ HierArrFloat _ret = %s; %s return _ret; }\n", v, rf);
                }
            } else if (ret == T_ARRAY_STRING) {
                /* promote up. A fresh value (literal/split/call) is built
                 * directly in the caller's arena; a bare variable is
                 * deep-copied (buffer + every element) into it — UNLESS the
                 * return-slot optimization already built it in _parent. */
                if (ret_must_copy(s->expr)) {
                    char *v = gen_expr(s->expr, "&_scope");
                    indent(o, ind); fprintf(o, "{ HierArrStr _ret = hier_arr_str_copy(_parent, %s); %s return _ret; }\n", v, rf);
                } else {
                    char *v = gen_expr(s->expr, "_parent");
                    indent(o, ind); fprintf(o, "{ HierArrStr _ret = %s; %s return _ret; }\n", v, rf);
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
                    indent(o, ind); fprintf(o, "{ %s_ret = hier_map_%s_copy(_parent, %s); %s return _ret; }\n", c_type(ret), map_fn(ret), v, rf);
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
            char *c = gen_expr(s->expr, scope);
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
            if (IS_OPT(st)) {
                Type inner = opt_inner(st);
                MatchArm *some = NULL, *none = NULL;
                for (int i = 0; i < s->narms; i++)
                    if (!strcmp(s->arms[i].variant, "Some")) some = &s->arms[i];
                    else none = &s->arms[i];
                indent(o, ind + 1); fprintf(o, "if (_m%d.has) {\n", mid);
                indent(o, ind + 2);
                int sborrow = type_is_heap(inner)
                    && !block_mutates(some->body, some->nbody, some->binds[0]);
                fprintf(o, "%sh_%s = %s;\n", c_type(inner), some->binds[0],
                        sborrow ? sfmt("_m%d.val", mid)
                                : copy_into(inner, scope, sfmt("_m%d.val", mid)));
                int m = cv_mark(); cv_push(some->binds[0], sborrow ? NULL : scope);
                gen_block(o, some->body, some->nbody, ind + 2, scope, ret);
                cv_restore(m);
                indent(o, ind + 1); fprintf(o, "} else {\n");
                gen_block(o, none->body, none->nbody, ind + 2, scope, ret);
                indent(o, ind + 1); fprintf(o, "}\n");
            } else if (IS_RES(st)) {   /* Ok(x) -> .okv / Err(e) -> .errv, tag is .ok */
                Type okt = res_ok(st), errt = res_err(st);
                MatchArm *okarm = NULL, *errarm = NULL;
                for (int i = 0; i < s->narms; i++)
                    if (!strcmp(s->arms[i].variant, "Ok")) okarm = &s->arms[i];
                    else errarm = &s->arms[i];
                indent(o, ind + 1); fprintf(o, "if (_m%d.ok) {\n", mid);
                indent(o, ind + 2);
                int okborrow = type_is_heap(okt)
                    && !block_mutates(okarm->body, okarm->nbody, okarm->binds[0]);
                fprintf(o, "%sh_%s = %s;\n", c_type(okt), okarm->binds[0],
                        okborrow ? sfmt("_m%d.okv", mid)
                                 : copy_into(okt, scope, sfmt("_m%d.okv", mid)));
                int m = cv_mark(); cv_push(okarm->binds[0], okborrow ? NULL : scope);
                gen_block(o, okarm->body, okarm->nbody, ind + 2, scope, ret);
                cv_restore(m);
                indent(o, ind + 1); fprintf(o, "} else {\n");
                indent(o, ind + 2);
                int errborrow = type_is_heap(errt)
                    && !block_mutates(errarm->body, errarm->nbody, errarm->binds[0]);
                fprintf(o, "%sh_%s = %s;\n", c_type(errt), errarm->binds[0],
                        errborrow ? sfmt("_m%d.errv", mid)
                                  : copy_into(errt, scope, sfmt("_m%d.errv", mid)));
                int m2 = cv_mark(); cv_push(errarm->binds[0], errborrow ? NULL : scope);
                gen_block(o, errarm->body, errarm->nbody, ind + 2, scope, ret);
                cv_restore(m2);
                indent(o, ind + 1); fprintf(o, "}\n");
            } else {   /* IS_ENUM: a tag dispatch; each arm binds its payload */
                EnumDef *ed = &g_enums[ENUM_ID(st)];
                const char *en = ed->name;
                for (int i = 0; i < s->narms; i++) {
                    MatchArm *arm = &s->arms[i];
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
            }
            indent(o, ind); fprintf(o, "}\n");
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
            g_loop_depth++;    /* moves are unsafe inside a loop (single read runs N times) */
            gen_block(o, s->body, s->nbody, ind + 2, ss, ret);
            g_loop_depth--;
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
    g_gen_ret = pr->ret;
    g_proc_body = pr->body; g_proc_nbody = pr->nbody;   /* for move-on-last-use read counts */
    g_loop_depth = 0;
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
    /* register ALL params so can_move_from never hands off a param's buffer */
    g_nparam = 0;
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
     * pointer dangling. Surfaced by self-hosting hierc0's resolve_nt, which
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
        /* an inout struct is a pointer to the caller's value — must NOT be
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
         * hier_copy_S_Ctx, the dominant residual cost). */
        if (IS_STRUCT(pt) && type_is_heap(pt) && !pr->params[i].is_inout
            && block_mutates(pr->body, pr->nbody, pr->params[i].name)) {
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
static int g_struct_color[128];
static int g_opt_color[256];
static int g_res_color[256];
static int g_tup_color[256];
static int g_emit_line;

static void emit_aggregate(FILE *o, Type t) {
    if (IS_STRUCT(t)) {
        int id = STRUCT_ID(t);
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
        if (o) fprintf(o, "struct HierOpt%d_ { char has; %sval; };\n", id, c_type(inner));
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
        if (o) fprintf(o, "struct HierRes%d_ { char ok; %sokv; %serrv; };\n",
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
            fprintf(o, "struct HierTup%d_ {", id);
            for (int j = 0; j < tt->n; j++) fprintf(o, " %s_%d;", c_type(tt->elems[j]), j);
            fprintf(o, " };\n");
        }
    }
}

/* Run the DFS purely to reject infinite (by-value self-containing) types, BEFORE
 * the resolver runs — type_is_heap recurses through fields and would otherwise
 * loop forever on such a type. */
static void check_finite_types(void) {
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

static void gen_program(FILE *o, ProcVec *prog) {
    fputs(HIER_RUNTIME, o);
    fputs("\n/* ---- generated from Hier source ---- */\n\n", o);
    /* Types reference one another, sometimes cyclically (a `[Node]` field is a
     * HierArrC descriptor holding S_Node*). Emit in dependency layers:
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
        fprintf(o, "typedef struct HierOpt%d_ HierOpt%d;\n", i, i);
    for (int i = 0; i < g_nrestypes; i++)           /* Result tags too */
        fprintf(o, "typedef struct HierRes%d_ HierRes%d;\n", i, i);
    for (int i = 0; i < g_ntuptypes; i++)           /* tuple tags too */
        fprintf(o, "typedef struct HierTup%d_ HierTup%d;\n", i, i);
    fputs("\n", o);
    /* (2) enum descriptors FIRST: a fixed { tag, ptr }, depends on nothing. They
     * must precede the composite-array typedefs below, because a `[Enum]` array
     * holds `E_Foo *data` and E_Foo is an anonymous-struct typedef that cannot
     * be forward-declared (unlike the struct/Option/Result/tuple tags above) —
     * so it has to be a complete type at the point the array typedef uses it.
     * This is the recursive-enum-with-array-of-itself case (e.g. an AST node
     * `enum Stmt: ... SIf(Expr, [Stmt], [Stmt])`). */
    for (int i = 0; i < g_nenums; i++)              /* forward-declare cells; a value is E_<name>* */
        fprintf(o, "typedef struct E_%s E_%s;\n", g_enums[i].name, g_enums[i].name);
    for (int i = 0; i < g_narrtypes; i++)           /* (2b) composite-array typedefs */
        fprintf(o, "typedef struct { %s*data; long len; long cap; } HierArrC%d;\n",
                c_type(g_arrtypes[i].elem), i);
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
                   "fprintf(stderr, \"hier: index %%ld out of bounds (len %%ld)\\n\", i, s->len); exit(1); } return i; }\n", i, i);
    }
    fputs("\n", o);
    /* (3) struct bodies + Option typedefs in containment order (infinite types
     * are rejected here). Enum descriptors above are complete, so a struct/Option
     * may embed an enum by value (it's only 2 words). */
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
    for (int i = 0; i < g_nenums; i++)
        for (int v = 0; v < g_enums[i].nvariants; v++) {
            Variant *var = &g_enums[i].variants[v];
            if (var->npayload == 0) continue;
            fprintf(o, "typedef struct {");
            for (int f = 0; f < var->npayload; f++)
                fprintf(o, " %sf%d;", c_type(var->payload[f]), f);
            fprintf(o, " } E_%s_%s;\n", g_enums[i].name, var->name);
        }
    /* (3c) enum cell bodies: a value is a pointer to { int tag; union of the
     * payload-bearing variants' field structs }. One allocation per node, fields
     * inline (no separate payload alloc, no void* indirection). Nullary variants
     * carry no fields and share a static singleton cell — zero per-node alloc and
     * dispatch stays a uniform v->tag. */
    for (int i = 0; i < g_nenums; i++) {
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
    }
    fputs("\n", o);
    for (int i = 0; i < g_nstructs; i++) {          /* (4) copy/eq prototypes */
        const char *nm = g_structs[i].name;
        if (type_is_heap(STRUCT_TYPE(i)))
            fprintf(o, "static S_%s hier_copy_S_%s(Arena *a, S_%s v);\n", nm, nm, nm);
        fprintf(o, "static int hier_eq_S_%s(S_%s a, S_%s b);\n", nm, nm, nm);
    }
    for (int i = 0; i < g_nopttypes; i++)           /* (4) Option-copy prototypes */
        if (type_is_heap(g_opttypes[i].inner))
            fprintf(o, "static HierOpt%d hier_opt%d_copy(Arena *a, HierOpt%d v);\n", i, i, i);
    for (int i = 0; i < g_nrestypes; i++)           /* (4) Result-copy prototypes */
        if (type_is_heap(T_RES_BASE + i))
            fprintf(o, "static HierRes%d hier_res%d_copy(Arena *a, HierRes%d v);\n", i, i, i);
    for (int i = 0; i < g_ntuptypes; i++)           /* (4) tuple-copy prototypes */
        if (type_is_heap(T_TUP_BASE + i))
            fprintf(o, "static HierTup%d hier_tup%d_copy(Arena *a, HierTup%d v);\n", i, i, i);
    for (int i = 0; i < g_nenums; i++) {            /* (4) enum copy/eq prototypes */
        const char *en = g_enums[i].name;
        if (type_is_heap(ENUM_TYPE(i)))
            fprintf(o, "static E_%s *hier_copy_E_%s(Arena *a, E_%s *v);\n", en, en, en);
        fprintf(o, "static int hier_eq_E_%s(E_%s *a, E_%s *b);\n", en, en, en);
    }
    for (int i = 0; i < g_nsoatypes; i++) {         /* (4) soa op prototypes (bodies are late) */
        const char *sn = g_structs[STRUCT_ID(g_soatypes[i].st)].name;
        fprintf(o, "static void Soa%d_push(Arena*, Soa%d*, S_%s);\n", i, i, sn);
        fprintf(o, "static Soa%d Soa%d_copy(Arena*, Soa%d);\n", i, i, i);
        fprintf(o, "static int Soa%d_eq(Soa%d, Soa%d);\n", i, i, i);
    }
    for (int i = 0; i < g_narrtypes; i++) {         /* (4) array-op prototypes */
        const char *ct = c_type(g_arrtypes[i].elem);
        fprintf(o, "static HierArrC%d hier_arr_C%d_with_cap(Arena*, long);\n", i, i);
        fprintf(o, "static void hier_arr_C%d_push(Arena*, HierArrC%d*, %s);\n", i, i, ct);
        fprintf(o, "static %shier_arr_C%d_get(HierArrC%d, long);\n", ct, i, i);
        fprintf(o, "static %s*hier_arr_C%d_ptr(HierArrC%d*, long);\n", ct, i, i);
        fprintf(o, "static void hier_arr_C%d_set(Arena*, HierArrC%d*, long, %s);\n", i, i, ct);
        fprintf(o, "static HierArrC%d hier_arr_C%d_copy(Arena*, HierArrC%d);\n", i, i, i);
        fprintf(o, "static int hier_arr_C%d_eq(HierArrC%d, HierArrC%d);\n", i, i, i);
    }
    fputs("\n", o);
    /* (5) deep-copy body per heap-bearing struct: re-home every heap field into
     * arena `a`. Non-heap fields are copied by the initial `r = v`. */
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
    /* (6) structural-equality body per struct, field-wise. */
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
    /* (7) composite-array op bodies (typedef already emitted in step 2). Each
     * deep-copies its elements through the same seam as the [string] array. */
    for (int i = 0; i < g_narrtypes; i++) {
        Type et = g_arrtypes[i].elem;
        const char *ct = c_type(et);              /* element C type (trailing space) */
        fprintf(o,
            "static HierArrC%d hier_arr_C%d_with_cap(Arena *a, long cap) {\n"
            "    HierArrC%d r; r.len = 0; r.cap = cap;\n"
            "    r.data = cap > 0 ? (%s*)arena_alloc(a, (size_t)cap * sizeof(%s)) : 0;\n"
            "    return r;\n}\n", i, i, i, ct, ct);
        fprintf(o,
            "static void hier_arr_C%d_push(Arena *a, HierArrC%d *xs, %sv) {\n"
            "    if (xs->len == xs->cap) {\n"
            "        long nc = xs->cap ? xs->cap * 2 : 4;\n"
            "        %s*nd = (%s*)arena_alloc(a, (size_t)nc * sizeof(%s));\n"
            "        for (long i = 0; i < xs->len; i++) nd[i] = xs->data[i];\n"
            "        if (xs->cap) arena_recycle(a, xs->data, (size_t)xs->cap * sizeof(%s));\n"  /* dead spine; element heap lives on via nd */
            "        xs->data = nd; xs->cap = nc;\n    }\n"
            "    xs->data[xs->len++] = %s;\n}\n",
            i, i, ct, ct, ct, ct, ct, copy_into(et, "a", "v"));
        fprintf(o,
            "static %shier_arr_C%d_get(HierArrC%d xs, long i) {\n"
            "    if (i < 0 || i >= xs.len) { fprintf(stderr, \"hier: index %%ld out of bounds (len %%ld)\\n\", i, xs.len); exit(1); }\n"
            "    return xs.data[i];\n}\n", ct, i, i);
        fprintf(o,   /* projection: a bounds-checked pointer into the buffer, so an */
            "static %s*hier_arr_C%d_ptr(HierArrC%d *xs, long i) {\n"   /* element is a mutable lvalue */
            "    if (i < 0 || i >= xs->len) { fprintf(stderr, \"hier: index %%ld out of bounds (len %%ld)\\n\", i, xs->len); exit(1); }\n"
            "    return &xs->data[i];\n}\n", ct, i, i);
        fprintf(o,
            "static void hier_arr_C%d_set(Arena *a, HierArrC%d *xs, long i, %sv) {\n"
            "    if (i < 0 || i >= xs->len) { fprintf(stderr, \"hier: index %%ld out of bounds (len %%ld)\\n\", i, xs->len); exit(1); }\n"
            "    xs->data[i] = %s;\n}\n", i, i, ct, copy_into(et, "a", "v"));
        fprintf(o,
            "static HierArrC%d hier_arr_C%d_copy(Arena *a, HierArrC%d src) {\n"
            "    HierArrC%d r = hier_arr_C%d_with_cap(a, src.len); r.len = src.len;\n"
            "    for (long i = 0; i < src.len; i++) r.data[i] = %s;\n"
            "    return r;\n}\n", i, i, i, i, i, copy_into(et, "a", "src.data[i]"));
        fprintf(o,
            "static int hier_arr_C%d_eq(HierArrC%d x, HierArrC%d y) {\n"
            "    if (x.len != y.len) return 0;\n"
            "    for (long i = 0; i < x.len; i++) if (!(%s)) return 0;\n"
            "    return 1;\n}\n\n", i, i, i, gen_eq(et, "x.data[i]", "y.data[i]"));
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
        if (type_is_heap(it))
            fprintf(o,
                "static HierOpt%d hier_opt%d_copy(Arena *a, HierOpt%d v) {\n"
                "    if (v.has) v.val = %s;\n"
                "    return v;\n}\n\n", i, i, i, copy_into(it, "a", "v.val"));
    }
    /* (8b) Result copy bodies: re-home only the active variant's value (the
     * inactive field is zero from the designated-initializer construction). */
    for (int i = 0; i < g_nrestypes; i++) {
        if (!type_is_heap(T_RES_BASE + i)) continue;
        Type okt = g_restypes[i].ok, errt = g_restypes[i].err;
        fprintf(o,
            "static HierRes%d hier_res%d_copy(Arena *a, HierRes%d v) {\n"
            "    if (v.ok) v.okv = %s;\n"
            "    else v.errv = %s;\n"
            "    return v;\n}\n\n", i, i, i,
            copy_into(okt, "a", "v.okv"), copy_into(errt, "a", "v.errv"));
    }
    /* (8c) tuple copy bodies: re-home each heap element field. */
    for (int i = 0; i < g_ntuptypes; i++) {
        if (!type_is_heap(T_TUP_BASE + i)) continue;
        TupType *tt = &g_tuptypes[i];
        fprintf(o, "static HierTup%d hier_tup%d_copy(Arena *a, HierTup%d v) {\n", i, i, i);
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
        EnumDef *ed = &g_enums[i];
        const char *en = ed->name;
        if (type_is_heap(ENUM_TYPE(i))) {
            fprintf(o, "static E_%s *hier_copy_E_%s(Arena *a, E_%s *v) {\n", en, en, en);
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
        fprintf(o, "static int hier_eq_E_%s(E_%s *a, E_%s *b) {\n", en, en, en);
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
    for (int i = 0; i < prog->n; i++) gen_proto(o, prog->v[i]);
    fputs("\n", o);
    for (int i = 0; i < prog->n; i++) gen_proc(o, prog->v[i]);
    fputs("int main(int argc, char **argv) {\n", o);
    fputs("    hier_argc = argc; hier_argv = argv;  /* exposed to the program via args() */\n", o);
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

/* directory of a path: "proj/geom/point.hi" -> "proj/geom"; "main.hi" -> "." */
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
static char *g_pkg_seen[256];   static int g_npkg_seen   = 0;   /* fully merged */
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
 * every .hi in `dir`, load its imports FIRST (post-order, paths relative to
 * `dir`), then full-parse this package's files and append the defs to *prog.
 * Each imported package's name is its import path's last component and its
 * files must declare exactly that (the dir match is structural); the main
 * package's directory may be named anything (it is reached by the entry file). */
static void merge_pkg(const char *dir, const char *pkgname, const char *prefix, ProcVec *prog) {
    char *key = canon_dir(dir);
    for (int i = 0; i < g_npkg_active; i++)
        if (!strcmp(g_pkg_active[i], key)) {
            fprintf(stderr, "hierc: import cycle through package `%s` (%s)\n", pkgname, dir);
            exit(1);
        }
    for (int i = 0; i < g_npkg_seen; i++)
        if (!strcmp(g_pkg_seen[i], key)) { free(key); return; }   /* shared dep already merged */
    if (g_npkg_active >= 64) { fprintf(stderr, "hierc: package nesting too deep\n"); exit(1); }
    g_pkg_active[g_npkg_active++] = key;

    DIR *d = opendir(dir);
    if (!d) { fprintf(stderr, "hierc: cannot open package directory %s\n", dir); exit(1); }
    char *files[512]; int nf = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        const char *nm = de->d_name;
        size_t L = strlen(nm);
        if (L > 3 && !strcmp(nm + L - 3, ".hi")) {
            if (nf >= 512) { fprintf(stderr, "hierc: too many files in package %s\n", dir); exit(1); }
            files[nf++] = sfmt("%s/%s", dir, nm);
        }
    }
    closedir(d);
    if (nf == 0) { fprintf(stderr, "hierc: package directory %s has no .hi files\n", dir); exit(1); }
    qsort(files, (size_t)nf, sizeof(char *), pkg_file_cmp);

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
        const char *slash = strrchr(path, '/');
        const char *childname = slash ? slash + 1 : path;
        char *childdir    = sfmt("%s/%s", dir, path);
        char *childprefix = sfmt("%s__", childname);
        merge_pkg(childdir, childname, childprefix, prog);
    }

    /* now full-parse this package's files: imported types are registered */
    for (int i = 0; i < nf; i++) {
        g_srcname = files[i];
        g_cur_pkg_prefix = prefix;
        ProcVec pv = parse_program(toks[i].v);
        if (!g_parsed_package) {
            fprintf(stderr, "hierc: %s is in package `%s` but has no `package` declaration\n", files[i], pkgname);
            exit(1);
        }
        if (strcmp(g_parsed_package, pkgname) != 0) {
            fprintf(stderr, "hierc: %s declares `package %s` but is in package `%s`\n", files[i], g_parsed_package, pkgname);
            exit(1);
        }
        for (int j = 0; j < pv.n; j++) {
            if (prog->n == prog->cap) {
                prog->cap = prog->cap ? prog->cap * 2 : 8;
                prog->v = (Proc **)realloc(prog->v, (size_t)prog->cap * sizeof(Proc *));
            }
            prog->v[prog->n++] = pv.v[j];
        }
    }
    g_cur_pkg_prefix = "";

    g_npkg_active--;                       /* pop the DFS path */
    g_pkg_seen[g_npkg_seen++] = key;       /* mark merged */
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
 * format for the stdin-only self-hosted compiler (hierc0), whose parser switches
 * its mangling prefix on each `package` header. Same traversal/ordering as
 * merge_pkg (imports first), so hierc0 sees definitions in dependency order. */
/* Emit a file, rewriting its leading `package <name>` header line to
 * `package main`. The entry package keeps the empty mangling prefix in hierc0
 * (which maps `package main` -> no prefix) regardless of its source name, just
 * as hierc gives the entry package prefix "". */
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
    char *key = canon_dir(dir);
    for (int i = 0; i < g_npkg_active; i++)
        if (!strcmp(g_pkg_active[i], key)) { fprintf(stderr, "hierc: import cycle at %s\n", dir); exit(1); }
    for (int i = 0; i < g_npkg_seen; i++)
        if (!strcmp(g_pkg_seen[i], key)) { free(key); return; }
    if (g_npkg_active >= 64) { fprintf(stderr, "hierc: package nesting too deep\n"); exit(1); }
    g_pkg_active[g_npkg_active++] = key;

    DIR *d = opendir(dir);
    if (!d) { fprintf(stderr, "hierc: cannot open package directory %s\n", dir); exit(1); }
    char *files[512]; int nf = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        const char *nm = de->d_name; size_t L = strlen(nm);
        if (L > 3 && !strcmp(nm + L - 3, ".hi")) {
            if (nf >= 512) { fprintf(stderr, "hierc: too many files in %s\n", dir); exit(1); }
            files[nf++] = sfmt("%s/%s", dir, nm);
        }
    }
    closedir(d);
    if (nf == 0) { fprintf(stderr, "hierc: package directory %s has no .hi files\n", dir); exit(1); }
    qsort(files, (size_t)nf, sizeof(char *), pkg_file_cmp);

    char *imp_paths[256]; int n_imp = 0;
    for (int i = 0; i < nf; i++) {
        char *s = read_file(files[i]);
        TokVec t = lex(s);
        scan_imports(t.v, imp_paths, &n_imp, 256);
    }
    for (int k = 0; k < n_imp; k++)
        bundle_pkg(sfmt("%s/%s", dir, imp_paths[k]), 0);   /* imported packages keep their names */
    for (int i = 0; i < nf; i++) {
        if (is_entry) emit_entry_file(read_file(files[i]));   /* entry -> `package main` (prefix "") */
        else          fputs(read_file(files[i]), stdout);
        fputc('\n', stdout);
    }
    g_npkg_active--;
    g_pkg_seen[g_npkg_seen++] = key;
}

int main(int argc, char **argv) {
    const char *input = NULL;
    const char *out   = NULL;
    const char *cc    = "cc";
    int emit_c_only = 0;
    int bundle = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--emit-c")) emit_c_only = 1;
        else if (!strcmp(argv[i], "--bundle")) bundle = 1;
        else if (!strcmp(argv[i], "--cc") && i + 1 < argc) cc = argv[++i];
        else if (argv[i][0] == '-') { fprintf(stderr, "hierc: unknown flag %s\n", argv[i]); return 1; }
        else input = argv[i];
    }
    if (!input) {
        fprintf(stderr, "usage: hierc file.hi [-o name] [--emit-c] [--bundle] [--cc <compiler>]\n");
        return 1;
    }
    g_srcname = input;

    if (bundle) {   /* emit the package's source as one post-order stream (for hierc0) */
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
    check_finite_types();   /* reject by-value-recursive types before the resolver */
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
