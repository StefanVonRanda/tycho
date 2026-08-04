/* tychoc - the Tycho compiler.
 *
 * Pipeline:  .ty source -> tokens (indentation-aware) -> AST -> type
 * resolution -> C source (with the Tycho runtime embedded verbatim) ->
 * invoke `cc` to produce a native binary.
 *
 * Usage:
 *   tychoc file.ty [-o name] [--emit-c] [--cc <compiler>]
 *     default: writes <base>.c and compiles it to <base> with `cc`.
 *     --emit-c: only write the C, do not compile. With -o it writes <name>.c;
 *               with no -o it writes the C to STDOUT rather than dropping a
 *               sibling .c beside the source.
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
#include <stdint.h>
#include <inttypes.h>
#include <dirent.h>
#include <unistd.h>
#include <locale.h>    /* newlocale/uselocale: the "C" LC_NUMERIC float literals are read and written in */
#include <float.h>     /* DBL_MAX/FLT_MAX: the float- and f32-literal overflow tests. NOT math.h/isinf -- tychoc links without -lm */

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

/* Format-checked like die_at/warn_at above: sfmt is the helper that bakes
 * literals into the emitted C, so a wrong conversion here miscompiles silently
 * rather than merely misprinting a diagnostic. `format(printf, 1, 2)` = fmt is
 * arg 1, the varargs start at arg 2. */
__attribute__((format(printf, 1, 2)))
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

/* --- float literals: read and written in the "C" locale, always -----------
 *
 * WHY THE COMPILER CARES ABOUT LC_NUMERIC. strtod and printf's "%g" both take
 * their decimal separator from LC_NUMERIC. Nothing in this tree calls setlocale,
 * so tychoc runs under "C" and a bare strtod/snprintf happens to be right -- but
 * that is an unstated dependency on a process-wide global. Note what does NOT
 * change it: setting LC_ALL in the environment. A C program starts in "C" until
 * something calls setlocale(LC_ALL, ""), so `LC_ALL=da_DK.UTF-8 tychoc prog.ty`
 * is inert and cannot be used to test this. What changes it is any linked C
 * library calling setlocale from a load-time constructor -- measured with an
 * LD_PRELOAD doing exactly that. Under a comma-decimal LC_NUMERIC the two sites
 * then broke in opposite directions:
 *
 *   READ  (lex_num below). strtod("1.5") stops at the '.', which is no longer
 *         the separator, and returns 1.0. The literal 1.5 becomes 1.
 *   WRITE (c_expr's E_FLOAT). snprintf("%.17g", 1.5) emits the bytes `1,5`, the
 *         ".0" guard scans for '.' and finds none, so it appends: `1,5.0`. In
 *         EXPRESSION position that is a legal COMMA EXPRESSION, so cc accepts it
 *         with no diagnostic and the value is wrong. Measured over the shapes
 *         codegen actually emits a literal into: `_l0.data[0] = 1,5.0;` silently
 *         stores 1, `(1,5.0 + 0.0)` is silently 5.0, and only a declarator
 *         initializer (`double _ret = 1,5.0;`) is a syntax error. The loud case
 *         exists but does not cover the others -- a wrong number in a compiled
 *         binary, from a correct source file, with nothing on stderr.
 *
 * Fixing one alone makes the round trip worse than leaving both: read-only-fixed
 * turns 1.5 into 5.0 where cc accepts it, write-only-fixed turns it into 1.0
 * everywhere. They are one defect.
 * runtime/tycho_rt.c@tycho_float_to_str is the same fix one layer down, for the
 * string a program PRINTS; this one is for the C source a compiler READS.
 *
 * WHY uselocale AND NOT snprintf_l/strtod_l. The _l forms are BSD/macOS
 * extensions in <xlocale.h>; glibc does not declare snprintf_l with or without
 * _GNU_SOURCE (measured for the runtime twin: `implicit declaration of function
 * 'snprintf_l'` under cc -std=c11). newlocale/uselocale/LC_NUMERIC_MASK are POSIX
 * 2008 and present on both, and the _GNU_SOURCE this file declares at the top
 * already exposes them -- no new feature-test macro. The handle is this
 * translation unit's own; it deliberately does NOT share the runtime's, which is
 * a separate translation unit compiled into a different program.
 *
 * FALLBACK, AND WHY IT IS STILL CORRECT. If newlocale fails at run time (no "C"
 * locale, out of memory) each direction converts under the ambient locale and
 * then translates the one separator by hand, taken from localeconv() -- C89,
 * always present. Same digits, same rounding; only the separator moves. A float
 * literal token is ASCII by construction (the lexer accepted only [0-9.eE+-]),
 * so rewriting its '.' is unambiguous.
 *
 * THE ".0" GUARD'S CONTRACT, AND WHY THE SCAN IS SOUND. At the write site the
 * guard answers "would this text be read back by `cc` as an INTEGER constant?"
 * and appends ".0" when it would, so the Tycho literal 3.0 emits `3.0` and
 * `3.0 / 2.0` is not integer division. It decides by scanning for the only things
 * %.17g can emit that make a C token non-integral: '.' (the separator), 'e'/'E'
 * (an exponent), and the 'n'/'i' of "nan"/"inf". That scan is sound ONLY because
 * the separator is now known to be '.': under any other separator the character
 * is absent from the set, every finite non-integral value looks integral to it,
 * and the guard appends ".0" to text that already had a fraction -- which is
 * exactly how `1,5` became `1,5.0`. The guard is correct because of the
 * conversion above it; changing one without the other reintroduces the bug. */
static locale_t c_numeric_handle(void) {
    static locale_t h;      /* tychoc is single-threaded: no pthread_once needed, */
    static int tried;       /* unlike the runtime twin, whose callers are tasks.  */
    if (!tried) { tried = 1; h = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0); }
    return h;               /* 0 on failure -- callers take the fallback leg */
}

/* The ambient LC_NUMERIC separator, which may be several bytes; "." when it is
 * already the C one. Fallback legs only. */
static const char *ambient_decimal_point(void) {
    struct lconv *lc = localeconv();
    return (lc && lc->decimal_point && lc->decimal_point[0]) ? lc->decimal_point : ".";
}

/* READ. Parse the float literal [s, end) as C would. */
static double c_strtod(const char *s, const char *end) {
    locale_t h = c_numeric_handle();
    if (h) {
        locale_t prev = uselocale(h);
        double v = strtod(s, NULL);
        uselocale(prev);            /* prev may be LC_GLOBAL_LOCALE; that is legal */
        return v;
    }
    const char *dp = ambient_decimal_point();
    if (dp[0] == '.' && dp[1] == '\0') return strtod(s, NULL);
    size_t dplen = strlen(dp), n = 0;
    char buf[512];
    for (const char *q = s; q < end && n + dplen + 1 < sizeof buf; q++) {
        if (*q == '.') { memcpy(buf + n, dp, dplen); n += dplen; }
        else buf[n++] = *q;
    }
    buf[n] = '\0';
    return strtod(buf, NULL);
}

/* WRITE. Format v into b as a C float literal body (no ".0" guard yet -- that is
 * the caller's, and the comment above says why the two belong together).
 * Returns the length. %.17g round-trips a double exactly. */
static int c_dtoa(char *b, size_t bs, double v) {
    locale_t h = c_numeric_handle();
    int m;
    if (h) {
        locale_t prev = uselocale(h);
        m = snprintf(b, bs, "%.17g", v);
        uselocale(prev);
        return m;
    }
    m = snprintf(b, bs, "%.17g", v);
    const char *dp = ambient_decimal_point();
    size_t dplen = strlen(dp);
    if (dplen == 1 && dp[0] == '.') return m;       /* already C-like */
    char *at = strstr(b, dp);
    if (!at) return m;                              /* integral, or inf/nan */
    *at = '.';                                      /* %g emits at most one separator */
    memmove(at + 1, at + dplen, (size_t)((b + m) - (at + dplen)) + 1);   /* +1 carries the NUL */
    return m - (int)dplen + 1;
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
    TK_LPAREN, TK_RPAREN, TK_LBRACKET, TK_RBRACKET, TK_COMMA, TK_ARROW, TK_SEMI,
    TK_FN, TK_RETURN, TK_IF, TK_ELIF, TK_ELSE, TK_FOR, TK_IN, TK_TRUE, TK_FALSE, TK_NULL, TK_STRUCT,
    TK_INOUT, TK_AMP, TK_AND, TK_OR, TK_NOT, TK_MATCH, TK_ENUM, TK_ORRETURN, TK_TYPE, TK_HANDLE,
    TK_BREAK, TK_CONTINUE,
    TK_SPAWN, TK_PARALLEL, TK_SELECT,
    TK_DOT, TK_ELLIPSIS, TK_DOTLT, TK_DOTDOT, TK_DOLLAR,
    TK_KW_INT, TK_KW_BOOL, TK_KW_STRING, TK_KW_FLOAT, TK_KW_PTR, TK_KW_BYTES,
    TK_KW_U32, TK_KW_U64, TK_KW_F32,
    TK_KW_U8, TK_KW_U16, TK_KW_I8, TK_KW_I16, TK_KW_I32, TK_KW_I64
} TokKind;

typedef struct {
    TokKind kind;
    char   *text;   /* identifier name, or raw string contents */
    int64_t ival;   /* HOST-WIDTH-SAFE: fixed 64-bit, never `long` — tycho `int` is 64-bit
                     * by spec, so an ILP32 host must still carry every literal exactly. */
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
    if (!strcmp(s, "inout"))  return TK_INOUT;
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
    if (!strcmp(s, "u8"))     return TK_KW_U8;
    if (!strcmp(s, "u16"))    return TK_KW_U16;
    if (!strcmp(s, "i8"))     return TK_KW_I8;
    if (!strcmp(s, "i16"))    return TK_KW_I16;
    if (!strcmp(s, "i32"))    return TK_KW_I32;
    if (!strcmp(s, "i64"))    return TK_KW_I64;
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
    int bracket_depth = 0;   /* (...) / [...] nesting: >0 joins physical lines (implicit continuation) */

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
        if (ws_sp && ws_tab && bracket_depth == 0)
            die_at(line, "mixed tabs and spaces in indentation; use one consistently");

        /* blank or comment-only line: skip without touching indentation */
        if (*p == '\n' || *p == '\0' || *p == '#') {
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
            continue;
        }

        /* emit INDENT / DEDENT for this logical line -- only at bracket depth 0.
         * Inside (...) / [...] a physical line is a continuation (Python-style
         * implicit line-join), so its leading whitespace is not indentation. */
        if (bracket_depth == 0) {
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
                /* `0x`/`0X` (hex) and `0b`/`0B` (binary) prefixes, accepted
                 * only when the integer part is exactly the single digit `0`
                 * -- so `10x` still lexes as an int followed by an identifier,
                 * the same tie-break as C/Go. A prefixed literal is ALWAYS an
                 * integer: there is no hexadecimal-float form, so the float
                 * checks below are skipped for it (docs/spec/01-lexical.md 3.9). */
                int litbase = 10;
                const char *digits = s;
                int prefixed = 0;
                if (p - s == 1 && *s == '0' &&
                    (*p == 'x' || *p == 'X' || *p == 'b' || *p == 'B')) {
                    int is_hex = (*p == 'x' || *p == 'X');
                    prefixed = 1;
                    litbase = is_hex ? 16 : 2;
                    digits = ++p;
                    while (is_hex ? isxdigit((unsigned char)*p)
                                  : (*p == '0' || *p == '1')) p++;
                    if (p == digits)
                        die_at(line, is_hex
                            ? "a hex literal needs at least one digit after `0x`"
                            : "a binary literal needs digits 0 or 1 after `0b`");
                }
                /* a '.' immediately followed by a digit makes it a float (D.D);
                 * an `e`/`E` exponent (optionally signed) does too, with or
                 * without a fractional part (1e10, 1.5e-3, 2E8). A bare trailing
                 * '.' stays a field token, and a malformed exponent (1e, 1.e5,
                 * 1e+) is NOT consumed -- the 'e...' lexes as a separate ident.
                 * No leading-dot form. */
                int is_float = 0;
                if (!prefixed && *p == '.' && isdigit((unsigned char)p[1])) {
                    is_float = 1;
                    p++;
                    while (isdigit((unsigned char)*p)) p++;
                }
                if (!prefixed && (*p == 'e' || *p == 'E')) {
                    const char *eq = p + 1;
                    if (*eq == '+' || *eq == '-') eq++;
                    if (isdigit((unsigned char)*eq)) {
                        is_float = 1;
                        p = eq;
                        while (isdigit((unsigned char)*p)) p++;
                    }
                }
                if (is_float) {
                    /* c_strtod, not strtod: under a comma-decimal LC_NUMERIC a
                     * bare strtod stops at the '.' and reads 1.5 as 1. See
                     * c_numeric_handle above -- this and the E_FLOAT emit are
                     * one defect with two sites. */
                    double dv = c_strtod(s, p);
                    /* OVERFLOW IS A TYCHO ERROR, UNDERFLOW IS NOT. `1e400` has no
                     * binary64 value: strtod returns +-HUGE_VAL, and codegen used to
                     * emit the bare token `inf`, which is not a C keyword and not a
                     * standard macro -- so the user wrote Tycho and cc answered
                     * "'inf' undeclared". Refusing it here matches the integer twin
                     * a few lines below (`integer literal out of range`) and loses no
                     * expressible value: docs/spec/03-types.md 5.2.2 makes infinity a
                     * legal float VALUE, reachable as 1.0/0.0, which this does not
                     * touch. What it rejects is a SPELLING nobody writes on purpose --
                     * a finite decimal silently becoming an infinity is the same class
                     * of defect as the locale bug above: correct source, wrong number,
                     * nothing on stderr.
                     * UNDERFLOW STAYS LEGAL. `1e-400` is below the smallest denormal,
                     * so its correctly-rounded binary64 value IS 0.0 -- ordinary
                     * IEEE-754 gradual underflow, still finite, and it compiles and
                     * behaves today. errno cannot tell the two apart (strtod sets
                     * ERANGE for both), which is why the test is on the VALUE.
                     * DBL_MAX, not isinf: the token grammar above accepted only
                     * [0-9.eE+-], so it can never spell "nan" -- a magnitude past
                     * DBL_MAX is therefore exactly an infinity -- and <float.h> is
                     * freestanding, while math.h's isinf can leave an undefined
                     * reference on a host that does not inline it -- Makefile@tychoc
                     * is the link recipe and it carries no -lm. */
                    if (dv > DBL_MAX || dv < -DBL_MAX)
                        die_at(line, "float literal out of range: `%.*s` exceeds the largest float "
                                     "(IEEE-754 binary64); write 1.0/0.0 for an infinity",
                               (int)(p - s), s);
                    tv_push(&out, (Tok){TK_FLOAT, NULL, 0, line, dv, tcol});
                } else {
                    int64_t v = 0;      /* fixed 64-bit, not `long`: on an ILP32 host a
                                         * `long` accumulator would reject every literal
                                         * above 2^31 that tycho `int` must represent. */
                    for (const char *q = digits; q < p; q++) {
                        int d = *q - '0';
                        if (litbase == 16 && d > 9) d = (*q | 32) - 'a' + 10;  /* lowercase-fold */
                        if (v > (INT64_MAX - d) / litbase) die_at(line, "integer literal out of range");
                        v = v * litbase + d;
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
                        /* `\r` is here because CRLF is the most common byte pair in HTTP
                         * and it used to cost a function call (`httpd.crlf()`).
                         * `\0` and `\xNN` are deliberately NOT in this set: the literal's
                         * text is pasted verbatim into a C string literal and interned by
                         * strlen (codegen `:8671`, `tycho_str_intern` at
                         * `runtime/tycho_rt.c:1005`), so a `\0` would truncate the
                         * interned length, and C's `\x` is greedy over hex digits so
                         * `"\x41" "1"` would mean `\x411`. Both need a byte-exact
                         * re-escaper plus a decoded length on the emit path. */
                        if (e != 'n' && e != 't' && e != 'r' && e != '\\' && e != '"')
                            die_at(line, "unsupported escape \\%c (use \\n \\t \\r \\\\ \\\")", e);
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

            if (c == '`') {
                /* RAW STRING LITERAL: `...` -- no escape is interpreted, so a
                 * backslash is a backslash and an embedded newline is a literal
                 * newline byte (the one genuinely multi-line literal form).
                 * There is no escape for a backtick, so a raw literal cannot
                 * contain one; that is the price of needing no escapes at all.
                 *
                 * The token this produces is an ORDINARY TK_STR whose text is
                 * ESCAPED source text, exactly like the `"..."` branch above:
                 * every byte that a C string literal cannot carry verbatim is
                 * re-escaped here into one of the four two-character escapes
                 * the language already has (\n \t \\ \"). Two consequences,
                 * both deliberate:
                 *   - codegen `:9003` pastes `sval` straight into a C string
                 *     literal, so it must be C-safe by the time it gets there;
                 *   - the adjacent-literal join at `:2234-2246` is sound on
                 *     this text for the reason stated there -- every escape is
                 *     exactly two characters -- so a raw piece MAY join with a
                 *     normal one (`` `a` "b" `` is one literal `ab`). Nothing
                 *     in the parser needed changing for that.
                 * `ival` stays 0: a raw literal is never an f-string, so no
                 * interpolation is performed on its text. */
                p++;                       /* skip the opening backtick */
                char rbuf[4096];
                int rn = 0;
                int startline = line;      /* diagnostics name the OPENING line */
                while (*p && *p != '`') {
                    const char *esc = NULL;
                    if (*p == '\n')      { esc = "\\n";  p++; line++; ls = p; }   /* keep line/col honest across the literal */
                    else if (*p == '\t') { esc = "\\t";  p++; }
                    else if (*p == '\\') { esc = "\\\\"; p++; }
                    else if (*p == '"')  { esc = "\\\""; p++; }
                    else if ((unsigned char)*p < 0x20)
                        die_at(line, "raw control byte in string literal (use an escape such as \\n or \\t)");
                    if (esc) {
                        if (rn + 2 >= (int)sizeof rbuf) die_at(startline, "string too long");
                        rbuf[rn++] = esc[0]; rbuf[rn++] = esc[1];
                    } else {
                        if (rn + 1 >= (int)sizeof rbuf) die_at(startline, "string too long");
                        rbuf[rn++] = *p++;
                    }
                }
                if (*p != '`') die_at(startline, "unterminated raw string literal");
                p++;                       /* skip the closing backtick */
                tv_push(&out, (Tok){TK_STR, xstrndup(rbuf, (size_t)rn), 0, startline, 0, tcol});
                continue;
            }

            if (c == '\'') {        /* char literal: 'x' or one escape -> one byte */
                p++;
                long cv;
                if (*p == '\\') {
                    p++;
                    /* `\xNN` takes EXACTLY two hex digits -- fixed width, never C's greedy form. It is
                     * legal HERE and still refused in a string literal for the reason `\0` is (`:376-382`):
                     * a char literal decodes to a byte in `ival` right here, while a string literal's text
                     * stays ESCAPED into codegen, where the join (`:4322`) needs every escape two chars. */
                    switch (*p) {
                        case 'n': cv = '\n'; break;  case 't': cv = '\t'; break;   case 'r': cv = '\r'; break;
                        case '0': cv = '\0'; break;  case '\\': cv = '\\'; break;  case '\'': cv = '\''; break;
                        case 'x': { const char *hx = "0123456789abcdef0123456789ABCDEF";   /* index %16 folds both cases */
                                    const char *d1 = p[1] ? strchr(hx, p[1]) : 0, *d2 = (p[1] && p[2]) ? strchr(hx, p[2]) : 0;
                                    if (!d1 || !d2) die_at(line, "\\x takes exactly two hex digits (e.g. '\\x41')");
                                    cv = 16 * ((d1 - hx) % 16) + (d2 - hx) % 16; p += 2; break; }
                        default: die_at(line, "unsupported char escape (use \\n \\t \\r \\0 \\xNN \\\\ \\')");
                    }
                    p++;
                } else if (*p && *p != '\'' && *p != '\n') { cv = (unsigned char)*p; p++; }
                else die_at(line, "empty or unterminated char literal");
                if (*p != '\'') die_at(line, "char literal must be exactly one character");
                p++;
                tv_push(&out, (Tok){TK_CHAR, NULL, cv, line, 0, tcol});
                continue;
            }

            /* operators (two-char first) */
            char c2 = p[1];
            TokKind k; int len = 1;
            if (c == '/' && c2 == '/') { g_err_col = tcol; die_at(line, "'//' is not valid in Tycho -- use '#' for comments, or '/' for division"); }
            if (c == '.' && c2 == '.' && p[2] == '.') { k = TK_ELLIPSIS; len = 3; }  /* variadic `...T` / spread `x...` */
            else if (c == '.' && c2 == '.' && p[2] == '<') { k = TK_DOTLT; len = 3; }  /* `0..<N`: half-open counting range (parallel for) */
            else if (c == '.' && c2 == '.') { k = TK_DOTDOT; len = 2; }  /* `1..9`: inclusive range in a scalar match arm */
            else if (c == ':' && c2 == ':')      { k = TK_COLONCOLON; len = 2; }
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
            else if (c == ';') k = TK_SEMI;     /* three-clause `for init; cond; post:` */
            else if (c == '&') k = TK_AMP;
            else if (c == '%') k = TK_PERCENT;
            else if (c == '|') k = TK_PIPE;
            else if (c == '^') k = TK_CARET;
            else if (c == '~') k = TK_TILDE;
            else if (c == '$') k = TK_DOLLAR;   /* generics: `$T` introduces a type parameter */
            else { g_err_col = tcol; die_at(line, "unexpected character '%c'", c); }
            if (k == TK_LPAREN || k == TK_LBRACKET) bracket_depth++;
            else if ((k == TK_RPAREN || k == TK_RBRACKET) && bracket_depth > 0) bracket_depth--;
            tv_push(&out, (Tok){k, NULL, 0, line, 0, tcol});
            p += len;
        }

        if (bracket_depth == 0)   /* inside (...) / [...] : join lines, emit no NEWLINE */
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
       /* the rest of the fixed-width integer family (first-class). u8/u16/i8/i16
        * are sub-int width, so C promotes them to `int` in arithmetic -- every
        * op producing one is cast back to its C type (trunc_ctype) to hold the
        * width invariant. i32/i64 map to C int/long long and wrap natively (-fwrapv). */
       T_U8, T_U16, T_I8, T_I16, T_I32, T_I64,
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

/* generics: the per-generic type-parameter cap (docs/spec/05-generics.md:20 --
 * "At most 16 type parameters and 16 size parameters may be introduced per
 * generic"). ONE number for functions (g_cur_typarams, :719), structs and enums.
 * Every fixed-size array indexed by a type-parameter NUMBER is sized by this
 * macro so the bound cannot be half-widened: StructDef/EnumDef `typarams` and
 * `from_args` below, the `_tp[]` staging arrays in parse_struct/parse_enum, and
 * the `args[]` type-argument list in parse_type_inner's generic application.
 * (Names are built with sfmt, which grows on the heap; `binds[]` is indexed by
 * the GLOBAL typaram id, not by this count, so neither is coupled to it.) */
#define TYCHO_MAX_TYPARAMS 16

typedef struct { char *name; Type type; } Field;
typedef struct { char *name; Field *fields; int nfields; int fields_cap; int line;
                 int generic; Type typarams[TYCHO_MAX_TYPARAMS]; int ntyparams;
                 int from_tmpl; Type from_args[TYCHO_MAX_TYPARAMS]; int nfrom_args; } StructDef;   /* generics: `struct Box($T)` template; instances are concrete copies with $T substituted. from_tmpl>=0 records the template+args this instance came from (for matching a recursive self-reference). */
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

/* Typed C handles (FFI R2): `handle Name:
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
/* size == 0 -> a dynamic composite array `[elem]`; size > 0 -> a fixed-size array
 * `[N]elem` (const generics, 1.6): stored inline (no heap), value-copied, static bounds.
 * A `[3]int` and a `[int]` are distinct interned entries (same elem, different size). */
/* bnd: this is a `bounded[N]T` — inline storage `{ T v[N]; long len; }` with a
 * runtime count and a push that traps on overflow. It reuses the fixed-array
 * `size` slot (size == capacity N > 0) but is a DISTINCT interned entry, so a
 * `bounded[4]int` and a `[4]int` never alias. */
/* HOST-WIDTH-SAFE: `size` is fixed 64-bit, never `long`. This is NOT cosmetic and
 * NOT a "sizes are small so anything fits" slot. A `[N]T` size is written by the
 * PARSER straight out of a token/const value (`fixn = cur(ps)->ival` and
 * `cap = cf->ival`, parse_type below), and `ival` is int64_t since the constant
 * folder was widened — so an arbitrary 64-bit literal reaches this field, and the
 * only validation on the way in is `> 0`. There is no upper bound. While this was
 * host `long`, an ILP32-hosted tychoc truncated it SILENTLY and fail-OPEN:
 * `[4294967297]int` emitted `v[1]` (2^32+1 mod 2^32) where LP64 emitted
 * `v[4294967297]`, and `[3000000000]int` wrapped negative and was rejected as
 * "a fixed-size array length must be positive". Reproduced on `gcc -m32`, fixed by
 * this retype, locked by tests/diag/fixarr_size_width.err.
 * The whole slot moves together, because these all alias this one field: the
 * `sizeparam_enc`/`sizeparam_id`/`g_sizebinds` NEGATIVE `[$N]T` encoding, the
 * `bounded_cap`/`fixarr_size` readers, and `GInst.spvals` — which feeds a size
 * back onto the VALUE path (`lit->ival = spvals[k]` in the const-generic
 * instantiator), so narrowing any one of them re-opens the truncation.
 * Consequence for printing: int64_t is `long` on LP64 but `long long` on ILP32, so
 * every format consuming `size` uses `%lld` with an explicit `(long long)` cast. */
typedef struct { Type elem; int64_t size; char bnd; } ArrType;
static ArrType *g_arrtypes;
static int g_narrtypes = 0, g_arrtypes_cap = 0;
#define IS_ARRC(t)  ((t) >= T_ARRC_BASE && (t) < T_OPT_BASE)   /* options sit above */
#define ARRC_ID(t)  ((int)((t) - T_ARRC_BASE))
static Type arrc_sized_b(Type elem, int64_t size, char bnd) {   /* find-or-create [elem] (size 0) / [size]elem / bounded[size]elem */
    if (IS_TASK(elem)) task_container_err();
    if (IS_HANDLE(elem)) handle_container_err();
    if (IS_CHAN(elem)) chan_container_err();
    for (int i = 0; i < g_narrtypes; i++)
        if (g_arrtypes[i].elem == elem && g_arrtypes[i].size == size && g_arrtypes[i].bnd == bnd) return T_ARRC_BASE + i;
    if (g_narrtypes >= T_OPT_BASE - T_ARRC_BASE) { fprintf(stderr, "tychoc: too many array types\n"); exit(1); }
    TBL_ENSURE(g_arrtypes, g_narrtypes, g_arrtypes_cap);
    g_arrtypes[g_narrtypes].elem = elem;
    g_arrtypes[g_narrtypes].size = size;
    g_arrtypes[g_narrtypes].bnd  = bnd;
    return T_ARRC_BASE + g_narrtypes++;
}
static Type arrc_sized(Type elem, int64_t size) { return arrc_sized_b(elem, size, 0); }
static Type arrc_of(Type elem) { return arrc_sized(elem, 0); }             /* dynamic [elem] */
static Type fixarr_of(Type elem, int64_t n) { return arrc_sized(elem, n); }   /* fixed [n]elem */
static Type bounded_of(Type elem, int64_t n) { return arrc_sized_b(elem, n, 1); }  /* bounded[n]elem */
#define IS_BOUNDED(t) (IS_ARRC(t) && g_arrtypes[ARRC_ID(t)].bnd)
static int64_t bounded_cap(Type t) { return g_arrtypes[ARRC_ID(t)].size; }
#define IS_FIXARR(t) (IS_ARRC(t) && g_arrtypes[ARRC_ID(t)].size > 0 && !g_arrtypes[ARRC_ID(t)].bnd)
/* const generics 1.6B: `[$N]T` — the size is a *parameter* (encoded as a NEGATIVE
 * size, see sizeparam_enc). Template-only: never a concrete fixed array, never emitted. */
#define IS_SIZEPARAM_ARR(t) (IS_ARRC(t) && g_arrtypes[ARRC_ID(t)].size < 0)
static int64_t fixarr_size(Type t) { return g_arrtypes[ARRC_ID(t)].size; }
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
/* A BIND VECTOR is indexed by the GLOBAL type-parameter id (`t - T_TYPARAM_BASE`),
 * NOT by a per-generic count -- so it must be as long as `g_typarams`, which grows
 * with the number of DISTINCT `$Name`s in the whole program and has no cap (see
 * typaram_of above). Every such vector used to be a fixed `Type binds[256]` local,
 * which a valid program with more than 256 distinct names overran (each individual
 * generic staying well under TYCHO_MAX_TYPARAMS): ASan reported a stack-buffer-
 * overflow WRITE at the `for (i < g_ntyparams) binds[i] = T_VOID` init loop. Raising
 * 256 would only move the cliff, so allocate at the table's current length instead.
 * `g_typarams` only ever grows and a vector is only ever indexed by an id interned
 * BEFORE it was allocated, so `g_ntyparams` at allocation time covers every index
 * that can legally be written. Never freed -- tychoc is one-shot and allocates this
 * way throughout (cf. `gi.binds`, which already used exactly this pattern). */
static Type *new_binds(void) {
    int n = g_ntyparams > 0 ? g_ntyparams : 1;
    Type *b = (Type *)xmalloc((size_t)n * sizeof(Type));
    for (int i = 0; i < n; i++) b[i] = T_VOID;   /* T_VOID == unbound */
    return b;
}
static char *g_cur_typarams[16];
static int   g_ncur_typarams = 0;

/* const generics 1.6B: a `$N` SIZE parameter (`[$N]T`). Encoded as a NEGATIVE size
 * in an arrc entry -- `size == -(id + 1)` -- so it lives in the same intern table
 * as `[T]` (size 0) and `[3]T` (size > 0). Interned by name like a `$T` type
 * parameter; bound to a concrete N at instantiation (from a `[3]T` argument), after
 * which the body sees `N` as an ordinary int const. Never reaches codegen. */
typedef struct { char *name; } SizeParam;
static SizeParam *g_sizeparams;
static int g_nsizeparams = 0, g_sizeparams_cap = 0;
static int64_t sizeparam_enc(char *name) {           /* find-or-create; returns the NEGATIVE size encoding for `[$name]T` */
    for (int i = 0; i < g_nsizeparams; i++)
        if (!strcmp(g_sizeparams[i].name, name)) return -(int64_t)(i + 1);
    TBL_ENSURE(g_sizeparams, g_nsizeparams, g_sizeparams_cap);
    int id = g_nsizeparams;
    g_sizeparams[id].name = name; g_nsizeparams++;
    return -(int64_t)(id + 1);
}
static int   sizeparam_id(int64_t enc) { return (int)(-enc - 1); }   /* decode the NEGATIVE size back to a table index */
/* Same shape as new_binds, for the `$N` side: indexed by sizeparam_id, so it must be
 * as long as the uncapped `g_sizeparams`. The old fixed `int64_t sizebinds[256]` local
 * clamped only its own init loop (`i < g_nsizeparams && i < 256`); match_type's
 * `g_sizebinds[sid] = cs_` had no such clamp, so >256 distinct `$N` names wrote past
 * the array. Allocated at the table's current length; never freed (one-shot). */
static int64_t *new_sizebinds(void) {
    int n = g_nsizeparams > 0 ? g_nsizeparams : 1;
    int64_t *b = (int64_t *)xmalloc((size_t)n * sizeof(int64_t));
    for (int i = 0; i < n; i++) b[i] = 0;   /* 0 == unbound (real sizes are > 0) */
    return b;
}
static char *g_cur_sizeparams[16];
static int   g_ncur_sizeparams = 0;
static int64_t *g_sizebinds = NULL;   /* during instantiate_generic: sizebinds[sizeparam_id] = concrete N (0 == unbound; real sizes are > 0) */

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
/* `name` is package-mangled ("net__Timeout"); `raw` is the name as written
 * ("Timeout"), kept so a NESTED match pattern can name a variant unqualified --
 * inside `Err(...)` the payload's enum type is already known, so no qualifier is
 * needed to disambiguate (see enum_variant_index). */
typedef struct { char *name; char *raw; Type payload[8]; int npayload; } Variant;
typedef struct { char *name; Variant *variants; int nvariants; int variants_cap; int line;
                 int generic; Type typarams[TYCHO_MAX_TYPARAMS]; int ntyparams;
                 int from_tmpl; Type from_args[TYCHO_MAX_TYPARAMS]; int nfrom_args; } EnumDef;   /* generics: `enum Tree($T)` template; instances substitute $T in variant payloads. from_tmpl>=0 records the template+args this instance came from (for matching a recursive self-reference). */
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
/* index of the variant of enum type `t` named `nm`, or -1. `nm` matches either the
 * mangled name (from a qualified `net.Timeout` nested pattern) or the name as
 * written (`Timeout`) -- unlike an expression, a nested pattern already knows the
 * payload's enum type, so an unqualified variant name is unambiguous there. */
static int enum_variant_index(Type t, const char *nm) {
    if (!IS_ENUM(t) || !nm) return -1;
    EnumDef *ed = &g_enums[ENUM_ID(t)];
    for (int v = 0; v < ed->nvariants; v++)
        if (!strcmp(ed->variants[v].name, nm) ||
            (ed->variants[v].raw && !strcmp(ed->variants[v].raw, nm))) return v;
    return -1;
}
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
/* fixed-width integer family (F). unsigned = u8/u16/u32/u64; the whole sized set
 * also includes signed i8/i16/i32/i64. `narrow` = sub-int width (u8/u16/i8/i16):
 * C promotes these to `int` in arithmetic, so an op producing one is cast back
 * to its C type (see trunc_result) to keep the value within its width. The
 * native `int` (C long) and i32/i64 (C int/long long) wrap on their own via
 * -fwrapv; u32/u64 wrap natively as unsigned. */
static int is_uint(Type t)      { return t == T_U8 || t == T_U16 || t == T_U32 || t == T_U64; }
static int is_sized_int(Type t) { return is_uint(t) || t == T_I8 || t == T_I16 || t == T_I32 || t == T_I64; }
/* Element-wise arithmetic on arrays (post-freeze): is `x OP y` legal for two
 * values of element type `et`? The whole rule is that `a OP b` on arrays is
 * legal IFF `a[i] OP b[i]` is legal, so this table is DERIVED from the scalar
 * arms of resolve_expr's binary chain (the arms that end in "arithmetic
 * requires two ints or two floats"), not invented:
 *
 *   int                       + - * / %   the int arm; % from the modulo/bitwise arm
 *   u8/u16/u32/u64/i8..i64    + - * / %   the sized-numeric arm; % likewise
 *   float                     + - * /     the float arm; % refused (not an integer)
 *   f32                       + - * /     the f32 arm;    % refused (not an integer)
 *   newtype over int/float    + - * /     the newtype arm; % refused -- the modulo arm
 *                                          tests `lt != T_INT`, true for a newtype,
 *                                          and is_sized_int() is false for one too
 *   char                      + -         the char arm (char±char -> char); no * / %
 *
 * Everything else (string, bytes, bool, struct, nested array, ...) has no
 * scalar arithmetic, so it gets none here. `&`, `|`, `^`, `<<` and `>>` are NOT
 * arithmetic and are deliberately out: they never reach this table, because the
 * bitwise and shift arms run first and refuse an array operand exactly as they
 * do today. */
static int elem_arith_ok(int op, Type et) {
    if (op == TK_PERCENT) return et == T_INT || is_sized_int(et);
    if (op != TK_PLUS && op != TK_MINUS && op != TK_STAR && op != TK_SLASH) return 0;
    if (et == T_INT || et == T_FLOAT || et == T_F32 || is_sized_int(et)) return 1;
    if (IS_NEWTYPE(et) && (nt_under(et) == T_INT || nt_under(et) == T_FLOAT)) return 1;
    if (et == T_CHAR) return op == TK_PLUS || op == TK_MINUS;
    return 0;
}
/* the five arithmetic operators, spelled for a diagnostic (op_str is a codegen
 * helper defined far below this point in the file). */
static const char *arith_op_spell(int op) {
    return op == TK_PLUS ? "+" : op == TK_MINUS ? "-" : op == TK_STAR ? "*"
         : op == TK_SLASH ? "/" : "%";
}
/* bit width of a sized/native integer type (for shift over-width guards). */
static int int_width(Type t) {
    if (t == T_U8  || t == T_I8)  return 8;
    if (t == T_U16 || t == T_I16) return 16;
    if (t == T_U32 || t == T_I32) return 32;
    return 64;   /* int, u64, i64 */
}
/* the sized-conversion builtins: to_u8/to_u16/to_u32/to_u64, to_i8/to_i16/to_i32/
 * to_i64, to_f32. Each casts any numeric scalar to the named type (truncate /
 * sign- or zero-extend, per the C cast). */
static Type sized_conv_target(const char *n) {
    if (!strcmp(n, "to_u8"))  return T_U8;
    if (!strcmp(n, "to_u16")) return T_U16;
    if (!strcmp(n, "to_u32")) return T_U32;
    if (!strcmp(n, "to_u64")) return T_U64;
    if (!strcmp(n, "to_i8"))  return T_I8;
    if (!strcmp(n, "to_i16")) return T_I16;
    if (!strcmp(n, "to_i32")) return T_I32;
    if (!strcmp(n, "to_i64")) return T_I64;
    return T_F32;   /* to_f32 */
}
static int is_sized_conv(const char *n) {
    return !strcmp(n,"to_u8")||!strcmp(n,"to_u16")||!strcmp(n,"to_u32")||!strcmp(n,"to_u64")||
           !strcmp(n,"to_i8")||!strcmp(n,"to_i16")||!strcmp(n,"to_i32")||!strcmp(n,"to_i64")||
           !strcmp(n,"to_f32");
}

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
/* closest field name in `sd` to a mistyped `name`, or NULL if none is plausible */
static const char *suggest_field(StructDef *sd, const char *name) {
    const char *best = NULL; int bestd = 99;
    for (int i = 0; i < sd->nfields; i++) dym(name, sd->fields[i].name, &best, &bestd);
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
/* hash() arg types seen at resolve (the generic `hash(x)` builtin). The map-key
 * emitter gates its per-type hash functions on struct_keyused; a type hashed via
 * hash() but never used as a map key needs those same functions, so this tracker
 * ORs into the emission gates. Empty unless a program actually calls hash(). */
static Type g_hashargs[64]; static int g_nhashargs = 0;
static int hash_keyused(Type st) {
    for (int i = 0; i < g_nhashargs; i++)
        if (mapkey_composite(g_hashargs[i]) && struct_in_key(st, g_hashargs[i])) return 1;
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
        case T_INT:          return "tycho_int ";
        case T_CHAR:         return "tycho_int ";
        case T_U32:          return "unsigned int ";        /* wraps at 2^32 natively */
        case T_U64:          return "unsigned long long ";  /* wraps at 2^64 natively */
        case T_F32:          return "float ";
        case T_U8:           return "unsigned char ";
        case T_U16:          return "unsigned short ";
        case T_I8:           return "signed char ";
        case T_I16:          return "short ";
        case T_I32:          return "int ";
        case T_I64:          return "long long ";
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
    /* A `$T` is REACHABLE here: an annotated local whose annotation names a
     * typaram not bound by any argument keeps the raw T_TYPARAM through
     * resolve, e.g. `fn f(a: $T) -> $T:` / `y: $U = a` -> "declared type $U
     * but value is int". Checked first because IS_TYPARAM is the only
     * unbounded-above range (T_TYPARAM_BASE = 65536, past every other base). */
    if (IS_TYPARAM(t))  return sfmt("$%s", typaram_name(t));
    if (IS_NEWTYPE(t)) return g_newtypes[NT_ID(t)].name;
    if (IS_TASK(t))    return sfmt("Task(%s)", type_name(task_inner(t)));
    if (IS_CHAN(t))    return sfmt("Channel(%s)", type_name(chan_inner(t)));
    if (IS_HANDLE(t))  return g_handles[HANDLE_ID(t)].name;
    if (IS_STRUCT(t)) return g_structs[STRUCT_ID(t)].name;
    if (IS_ARRC(t)) {   /* [T] dynamic, [N]T fixed (1.6), [$N]T size-param (1.6B), bounded[N]T */
        int64_t sz = g_arrtypes[ARRC_ID(t)].size;
        if (IS_BOUNDED(t)) return sfmt("bounded[%lld]%s", (long long)sz, type_name(arr_elem(t)));
        if (sz > 0) return sfmt("[%lld]%s", (long long)sz, type_name(arr_elem(t)));
        if (sz < 0) return sfmt("[$%s]%s", g_sizeparams[sizeparam_id(sz)].name, type_name(arr_elem(t)));
        return sfmt("[%s]", type_name(arr_elem(t)));
    }
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
        case T_VOID:         return "void";
        case T_NONE:         return "None";
        case T_OK_PARTIAL:   return "Ok(...)";
        case T_ERR_PARTIAL:  return "Err(...)";
        case T_INT:          return "int";
        case T_CHAR:         return "char";
        case T_U32:          return "u32";
        case T_U64:          return "u64";
        case T_F32:          return "f32";
        case T_U8:           return "u8";
        case T_U16:          return "u16";
        case T_I8:           return "i8";
        case T_I16:          return "i16";
        case T_I32:          return "i32";
        case T_I64:          return "i64";
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
        /* Every tag of the base enum (T_VOID..T_PENDING) is now cased above
         * except T_PENDING, so T_PENDING is the only thing that can land here.
         * It is UNREACHABLE: a T_PENDING never escapes as an expression's
         * resolved type -- resolve_expr dies with a dedicated message at the
         * first use that needs the type (:4592), pend_ground rejects a pending
         * grounding type BEFORE its two type_name calls (:4396), and
         * resolve_block audits any still-pending decl at block end. Verified
         * empirically: an instrumented build over all 370 tests/, tests/reject/,
         * examples/, corelib/ and compiler/ sources plus 7 targeted pending
         * probes recorded zero arrivals. Returning "void" is therefore a
         * fail-safe for a state that cannot occur, not a description of a type:
         * if it ever does fire, the message is wrong but not unsound. */
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
               E_SPREAD,   /* x... : spread an array into a variadic parameter (lhs=the array); rejected elsewhere */
               E_SLICE,    /* xs[a:b]: a sub-range view (lhs=array, rhs=lo or NULL, args[0]=hi or NULL) */
               E_LAMBDA,   /* fn(p)->r: e closure literal; ival indexes g_laminfo */
               E_NULL,     /* `null`: the opaque ptr literal (void*)0 (FFI) */
               E_SPAWN     /* spawn f(args): run the call on a new thread; lhs = the E_CALL, ival = spawn-site id */ } ExprKind;

typedef struct Expr Expr;
struct Expr {
    ExprKind kind;
    Type     type;     /* filled in by resolver */
    int      line;
    int64_t  ival;     /* E_INT / E_BOOL — fixed 64-bit, host-width-independent */
    double   fval;     /* E_FLOAT */
    char    *sval;     /* E_STR contents / E_IDENT name / E_CALL callee */
    TokKind  op;       /* E_BINOP */
    Expr    *lhs, *rhs;
    Expr   **args; int nargs;   /* E_CALL */
    char   **argnames; /* E_CALL: parallel to args -- non-NULL entry = named field (struct construction `P(x: 1)`); NULL ptr / all-NULL entries = positional */
    const char *pkg;   /* E_CALL: prefix of the package this call appears in ("" = main); used to resolve a package-local name */
    const char *qual;  /* E_CALL: explicit qualifier of `pkg.name(...)` (the source ident, e.g. "geom"); NULL if unqualified */
    Type   *typeargs; int ntypeargs;   /* E_CALL: explicit call-site type args `name$(int, ...)`; 0 = none (inferred) */
    int     pkg_done;  /* E_CALL: this node's `pkg.name` -> `pkg__name` rewrite already ran. Resolution is NOT
                        * single-pass: instantiate_generic (and the generic struct/enum paths) resolve every
                        * argument once to infer `$T`, then the concrete-signature loop resolves the SAME node
                        * again against the bound parameter type. e->sval is rewritten in place and e->qual is
                        * kept, so a second pass would mangle the mangled name (`net.net__port_of`) and report
                        * "package 'net' has no symbol 'net__port_of'". This latch makes the rewrite idempotent. */
};

typedef enum { S_DECL, S_ASSIGN, S_RETURN, S_IF, S_WHILE, S_FORRANGE,
               S_FOR3, /* three-clause `for init; cond; post:` — see the note on `els` below */
               S_INDEXSET, S_FIELDSET, S_EXPR, S_MATCH, S_MDECL, S_MASSIGN,
               S_BREAK, S_CONTINUE,
               S_CONST, /* `const NAME = <literal>` local: folded at use, no runtime storage */
               S_SELECT /* select over channel recv arms + default/closed (CC-5) */ } StmtKind;

typedef struct Stmt Stmt;
/* one arm of a `match`: a variant name (Some/None or an enum variant), the
 * names it binds from the payload, and its block.
 *
 * `sub` is ONE level of NESTED pattern on the single payload of an `Ok`/`Err`/
 * `Some` arm -- `Err(net.Timeout)`, `Err(Timeout)`, `Err(C(n))` -- holding the
 * inner variant's name as written (mangled when the pattern was qualified);
 * `subbinds` are the names that inner variant's payload binds. NULL means the arm
 * binds its payload plainly (`Err(e)`).
 *
 * A bare `Err(A)` is ambiguous at parse time -- binding, or nullary variant? It
 * parses as a binding and the RESOLVER promotes it to a `sub` once the payload
 * type is known (match_arm_payload). Before 2026-07-26 there was no promotion and
 * no error: the arm bound a variable named `A` and therefore matched EVERY `Err`,
 * surfacing only as "duplicate Err arm" if a second arm existed (FRICTION.md:139).
 * `sub_vi` is the resolved variant index, or -1 for an unrefined arm; codegen
 * reads it rather than recomputing, so the arm is visited once. */
typedef struct { char *variant; char *binds[8]; int nbinds;
                 char *sub; char *subbinds[8]; int nsubbinds; int sub_line; int sub_vi;
                 Stmt **body; int nbody; int line;
                 /* Scalar-match arms (variant == NULL): up to 8 pattern elements,
                  * each a literal or a range `plo..phi`. pkind: 0 int literal,
                  * 1 char literal, 2 bool (plo/phi hold 0/1), 3 a const NAME that
                  * the resolver folds to a value (pcname/pch, then rewritten to
                  * pkind 0 in place). The resolver also rewrites a bare const-
                  * name arm (parsed through the ident path) to pn=1 here. */
                 int pn; int pkind[8]; int64_t plo[8], phi[8];
                 char *pcname[8], *pch[8]; } MatchArm;
struct Stmt {
    StmtKind kind;
    int      line;
    char    *name;         /* S_DECL / S_ASSIGN target, or S_FORRANGE loop var */
    Type     decl_type;    /* S_DECL resolved type */
    int      typed_decl;   /* S_DECL: had an explicit type annotation */
    Type     annot;        /* explicit annotation when typed_decl */
    Expr    *expr;         /* value / condition / return / S_INDEXSET rhs / match scrutinee */
    Expr    *target;       /* S_INDEXSET lvalue (an E_INDEX) */
    Expr    *r_start, *r_stop;            /* S_FORRANGE bounds; the loop steps by 1.
                                          * HISTORY: an `r_step` field sat beside these
                                          * until 2026-07-30. `range(a,b,step)` was its
                                          * only producer and went on 2026-07-29, so it
                                          * had been ALWAYS NULL since; `0..<N` and the
                                          * foreach/parfor desugarings all step by 1. It
                                          * went with its codegen -- the loops-cleanup plan. */
    char    *names[8]; int nnames;       /* S_MDECL targets: `a, b := f()` */
    Type     mtypes[8];                  /* S_MDECL resolved element types */
    Stmt   **body; int nbody;
    /* S_FOR3 reuses the generic sub-statement slots rather than adding fields of
     * its own, and the split is not arbitrary. `expr` is the condition (as
     * S_WHILE). The POST clause is the LAST element of `body` because it runs on
     * every iteration, so every per-iteration analysis in this file -- append
     * fusion, block_mutates, count_reads_b, clone_block, the escape scan -- must
     * see it, and all of them walk `body` generically. The INIT clause is
     * `els[0]` (nels == 1) because it runs once, before the loop, like a
     * prelude; the same generic walkers still reach it via `els`. Adding
     * `Stmt *init, *post` instead would have hidden both subtrees from ~20
     * walkers that would each need a new line. Codegen names them
     * `s->els[0]` and `s->body[s->nbody-1]`. */
    Stmt   **els;  int nels;
    MatchArm *arms; int narms;           /* S_MATCH / S_SELECT (variant = "recv"/"default"/"closed") */
    Expr   **sel_ch;                     /* S_SELECT: per-arm channel expr (NULL for default/closed) */
    Stmt    *ctrl;                       /* value if/match: `x := if.../match...` — the S_IF/S_MATCH whose single-expr branch tails feed this decl (only set on S_DECL; other tail positions desugar at parse time) */
    int      parallel;                   /* S_FORRANGE: `parallel for` (CC-3) */
    int      foreach;                    /* S_FORRANGE parallel: deferred `parallel for x in EXPR` (name=var, r_start=src ident, body=raw); resolve_parfor type-branches array vs channel */
    int      par_id;                     /* S_FORRANGE parallel: index into g_parfor */
};

typedef struct { char *name; Type type; int is_inout; int is_sink; int is_variadic; const char *ffi_ct; } Param;   /* is_variadic: `xs: ...T` — type is [T]; a call packs its trailing args into it */   /* ffi_ct: FFI-boundary sized C type ("unsigned int " etc.) for a u8/u16/.../i64 extern param — NULL = use c_type(type) (which is int) */

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
    char   *sizeparams[16];/* const generics 1.6B: `[$N]T` size-param names, first-appearance order (bound to an int const in the body) */
    int     nsizeparams;
    const char *srcfile;   /* package mode: the file this proc was parsed from, for diagnostics (NULL = use the global) */
    const char *srctext;   /* package mode: that file's source text, for die_at's snippet (NULL = use the global) */
} Proc;

typedef struct { Proc **v; int n, cap; } ProcVec;

/* Package mode: point diagnostics (g_srcname / g_src for die_at) at a proc's own
 * source file -- a proc lives entirely in one file, so this is the right grain
 * for resolve/codegen errors. A NULL srcfile (single-file build, or a synthesized
 * proc) leaves the globals as they are. */
static void diag_use_proc(const Proc *pr) {
    if (pr && pr->srcfile) { g_srcname = pr->srcfile; g_src = pr->srctext; }
}

/* A user-defined projection (2.4): `subscript name(p...) -> inout U: yield &<place>`.
 * Not a function -- it yields a PLACE into one of its arguments. At a call site the
 * yielded place is inlined with the args substituted for the params (a compile-time
 * place-macro; no runtime object), then the surrounding read/write flows through the
 * existing lvalue machinery. `place` is the inner of the `yield &<place>` (the E_ADDR
 * is stripped at parse). Invariants (checked): the place is rooted in a parameter (so
 * the projection can't dangle) and each parameter appears in it at most once (so an
 * argument is never double-evaluated on substitution). */
typedef struct {
    char   *name;
    Param  *params; int nparams;
    Type    ret;            /* the projected type U (`-> inout U`) */
    Expr   *place;          /* the yielded place expression (E_ADDR stripped) */
    int     line;
} Subscript;
static Subscript *g_subs = NULL; static int g_nsubs = 0, g_nsubs_cap = 0;

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
    if (is_array(t)) {
        Type se = subst_type(arr_elem(t), binds);
        if (IS_ARRC(t)) {
            int64_t sz = g_arrtypes[ARRC_ID(t)].size;
            if (sz < 0) {   /* [$N]T (1.6B): substitute the bound concrete N; if still unbound, stay a size-param */
                int64_t cn = (g_sizebinds && g_sizebinds[sizeparam_id(sz)] > 0) ? g_sizebinds[sizeparam_id(sz)] : 0;
                return cn > 0 ? fixarr_of(se, cn) : arrc_sized(se, sz);
            }
            if (sz > 0) return fixarr_of(se, sz);   /* [3]$T -> [3]int: preserve the fixed size */
        }
        return arr_of(se);   /* dynamic: arr_of canonicalizes [int]->T_ARRAY_INT */
    }
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
    if (IS_SIZEPARAM_ARR(t)) return 1;   /* const generics 1.6B: `[$N]T` is template-only, transient like a `$T` */
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

/* const generics 1.6B: does `t` (recursively) mention a `$N` size parameter? A
 * `[$N]T` is only meaningful in a generic function's parameter type (where the
 * argument fixes N); in any *stored* position (struct field, enum payload, newtype)
 * there is nothing to infer N from, so it is rejected -- fail closed (RULE 5). */
static int type_has_sizeparam(Type t) {
    if (IS_SIZEPARAM_ARR(t)) return 1;
    if (is_array(t)) return type_has_sizeparam(arr_elem(t));
    if (IS_OPT(t))  return type_has_sizeparam(opt_inner(t));
    if (IS_RES(t))  return type_has_sizeparam(g_restypes[RES_ID(t)].ok) || type_has_sizeparam(g_restypes[RES_ID(t)].err);
    if (is_map(t))  return type_has_sizeparam(map_key(t)) || type_has_sizeparam(map_val(t));
    if (IS_FUNC(t)) {
        if (type_has_sizeparam(func_ret(t))) return 1;
        for (int i = 0; i < func_n(t); i++) if (type_has_sizeparam(func_param(t, i))) return 1;
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
    if (is_array(pat) && is_array(concrete)) {
        int64_t ps_ = IS_ARRC(pat)      ? g_arrtypes[ARRC_ID(pat)].size      : 0;   /* built-in arrays (T_ARRAY_INT ...) are dynamic: size 0 */
        int64_t cs_ = IS_ARRC(concrete) ? g_arrtypes[ARRC_ID(concrete)].size : 0;
        if (ps_ < 0) {                          /* [$N]T pattern (1.6B): bind N; the argument must be a fixed array */
            if (cs_ <= 0 || !g_sizebinds) return 0;
            int sid = sizeparam_id(ps_);
            if (g_sizebinds[sid] > 0 && g_sizebinds[sid] != cs_) return 0;   /* N already bound to a different size */
            g_sizebinds[sid] = cs_;
        } else if (ps_ != cs_) {
            return 0;                           /* [3]T vs [4]T, or fixed vs dynamic: distinct types */
        }
        return match_type(arr_elem(pat), arr_elem(concrete), binds);
    }
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
        char *vraw  = g_enums[tmpl].variants[v].raw;
        int   np    = g_enums[tmpl].variants[v].npayload;
        Type  pl[8];
        for (int f = 0; f < np; f++)
            pl[f] = subst_type(g_enums[tmpl].variants[v].payload[f], binds);   /* may realloc g_enums */
        EnumDef *e = &g_enums[id];
        TBL_ENSURE(e->variants, e->nvariants, e->variants_cap);
        e = &g_enums[id];
        Variant *dst = &e->variants[e->nvariants];
        dst->name = vname; dst->raw = vraw; dst->npayload = np;
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
static Expr *consts_find(const char *name);   /* fwd: a `[W]T` fixed-array size may name an int const */
/* could this token begin a type? (used to disambiguate `[W]int` size-form from a dynamic `[Foo]`) */
static int tok_starts_type(TokKind k) {
    return k == TK_IDENT || k == TK_LBRACKET || k == TK_DOLLAR || k == TK_LPAREN ||
           k == TK_KW_INT || k == TK_KW_FLOAT || k == TK_KW_BOOL || k == TK_KW_STRING ||
           k == TK_KW_PTR || k == TK_KW_BYTES || k == TK_KW_U32 || k == TK_KW_U64 || k == TK_KW_F32 ||
           k == TK_KW_U8 || k == TK_KW_U16 || k == TK_KW_I8 || k == TK_KW_I16 || k == TK_KW_I32 || k == TK_KW_I64;
}
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
    if (t->kind == TK_IDENT && !strcmp(t->text, "bounded")) {   /* bounded[N]T: inline fixed-cap, variable-count */
        ps->p++;
        eat(ps, TK_LBRACKET, "'[' after bounded");
        int64_t cap;   /* capacity N: an int literal or an int `const` name, same as a fixed [N]T */
        if (at(ps, TK_INT)) { cap = cur(ps)->ival; ps->p++; }
        else if (at(ps, TK_IDENT)) {
            Expr *cf = consts_find(pkg_mangle(cur(ps)->text));
            if (!cf || cf->kind != E_INT)
                die_at(cur(ps)->line, "a bounded capacity must be an integer literal or an int `const` -- '%s' is not", cur(ps)->text);
            cap = cf->ival; ps->p++;
        } else die_at(cur(ps)->line, "bounded needs a capacity: bounded[N]T");
        if (cap <= 0) die_at(t->line, "a bounded capacity must be positive");
        eat(ps, TK_RBRACKET, "']' after the bounded capacity");
        Type belem = parse_type(ps);
        if (belem == T_VOID || belem == T_BOOL)   /* mirror [N]bool: no bounded-bool codegen */
            die_at(t->line, "a bounded element cannot be bool or void");
        return bounded_of(belem, cap);
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
    if (t->kind == TK_LBRACKET) {        /* [int] / [string] / [string: int] / [N]T fixed */
        ps->p++;
        /* [N]T fixed-size array (const generics, 1.6): a size before the element type.
         * N is an int literal (`[3]int`) or an int `const` name (`[W]int`). A bare `[T]`
         * (element right after `[`) stays a dynamic array. `[3]` (int can't be a type) is
         * unambiguous; `[W]int` treats W as a size iff an element type follows it. */
        /* The size N is INSIDE the brackets, the element T follows the `]` (`[3]int`).
         * N is an int literal or an int `const`. `[3]int` is unambiguous; `[W]int` vs a
         * dynamic `[Foo]` is disambiguated by whether a type follows the `]`. */
        /* [$N]T generic-over-size (const generics 1.6B): a `$N` size parameter, its
         * length inferred from the argument at each call (`[$N]int`; in the body N is
         * an int const). Distinguished from a dynamic `[$T]` (type-param element) by a
         * TYPE following `]`. The one non-type IDENT that can follow a complete type in
         * a signature is the contextual keyword `where` (`-> [$T] where cmp(T)`), so it
         * is excluded -- otherwise a dynamic `[$T]` return would be misread as `[$T]where`. */
        if (at(ps, TK_DOLLAR) && peek(ps, 1)->kind == TK_IDENT &&
            peek(ps, 2)->kind == TK_RBRACKET && tok_starts_type(peek(ps, 3)->kind) &&
            !(peek(ps, 3)->kind == TK_IDENT && !strcmp(peek(ps, 3)->text, "where"))) {
            ps->p++;                             /* eat '$' */
            Tok *snm = eat(ps, TK_IDENT, "a size-parameter name after '$'");
            int seen = 0;
            for (int i = 0; i < g_ncur_sizeparams; i++) if (!strcmp(g_cur_sizeparams[i], snm->text)) { seen = 1; break; }
            if (!seen) {
                if (g_ncur_sizeparams >= 16) die_at(snm->line, "too many size parameters (max 16)");
                g_cur_sizeparams[g_ncur_sizeparams++] = snm->text;
            }
            int64_t enc = sizeparam_enc(snm->text);
            eat(ps, TK_RBRACKET, "']'");
            Type felem = parse_type(ps);
            if (felem == T_VOID || felem == T_BOOL)   /* mirror bounded: no fixed-array bool codegen. Dynamic [bool] IS legal (tests/bool_array.ty) */
                die_at(t->line, "a fixed-size array element cannot be bool or void -- a dynamic [bool] is legal");
            return arrc_sized(felem, enc);
        }
        int size_is_int   = at(ps, TK_INT) && peek(ps, 1)->kind == TK_RBRACKET;
        int size_is_const = at(ps, TK_IDENT) && peek(ps, 1)->kind == TK_RBRACKET &&
                            tok_starts_type(peek(ps, 2)->kind);
        if (size_is_int || size_is_const) {
            int64_t fixn;
            if (size_is_int) { fixn = cur(ps)->ival; ps->p++; }
            else {
                Expr *cf = consts_find(pkg_mangle(cur(ps)->text));
                if (!cf || cf->kind != E_INT)
                    die_at(cur(ps)->line, "a fixed-size array length must be an integer literal or an int `const` -- '%s' is not", cur(ps)->text);
                fixn = cf->ival; ps->p++;
            }
            if (fixn <= 0) die_at(t->line, "a fixed-size array length must be positive");
            eat(ps, TK_RBRACKET, "']'");
            Type felem = parse_type(ps);
            if (felem == T_VOID || felem == T_BOOL)   /* same rule and same wording as the [$N]T site above */
                die_at(t->line, "a fixed-size array element cannot be bool or void -- a dynamic [bool] is legal");
            return fixarr_of(felem, fixn);
        }
        Type elem = parse_type(ps);
        if (at(ps, TK_COLON)) {          /* map type: [K: V] */
            ps->p++;
            Type val = parse_type(ps);
            eat(ps, TK_RBRACKET, "']'");
            if (has_typaram(elem) || has_typaram(val))   /* generics: a `[$K: $V]` pattern -- key/value validity is checked at instantiation */
                return mapc_of(elem, val);
            Type mt = map_of(elem, val);   /* map_of routes composite values to mapc_of; only a bad key is T_VOID */
            if (mt == T_VOID)
                die_at(t->line, "map keys must be string, int (directly or through a newtype), a fieldless enum, or a hashable struct/tuple/array");
            return mt;
        }
        eat(ps, TK_RBRACKET, "']'");
        if (elem == T_VOID)   /* defensive, not reachable from source: parse_type_inner's only `return T_VOID` (src/tychoc.c:2161) sits after a die_at */
            die_at(t->line, "an array element type cannot be void -- every other type is allowed, including bytes, a tuple, a map and Option");
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
                Type args[TYCHO_MAX_TYPARAMS];
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
                Type *binds = new_binds();
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
                Type args[TYCHO_MAX_TYPARAMS];
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
                Type *binds = new_binds();
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
        case TK_KW_U8:     ps->p++; return T_U8;
        case TK_KW_U16:    ps->p++; return T_U16;
        case TK_KW_I8:     ps->p++; return T_I8;
        case TK_KW_I16:    ps->p++; return T_I16;
        case TK_KW_I32:    ps->p++; return T_I32;
        case TK_KW_I64:    ps->p++; return T_I64;
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
            TokVec tv = lex(sub);
            /* lex() restarts its own line counter at 1 (see `int line = 0;` at the top
             * of lex), so every node parsed out of a hole would carry line 1 — a
             * diagnostic inside `f"{len(x)}"` on line 6 pointed at line 6's file line 1
             * (often a comment). The hole's text all belongs to `line` in the real file,
             * so stamp it onto the sub-tokens before parsing; col is hole-relative and
             * meaningless against the real line, so drop it (0 = no caret). */
            for (int k = 0; k < tv.n; k++) { tv.v[k].line = line; tv.v[k].col = 0; }
            Parser sp = { tv.v, 0, 0 }; Expr *ex = parse_expr(&sp);
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
            pr->params[pr->nparams].is_variadic = 0;   /* a lambda parameter is never variadic */
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
        /* ADJACENT STRING LITERALS JOIN (C / Python rule): `"a" "b"` is the one
         * literal `"ab"`. With the implicit line-join already inside (...) / [...]
         * (tests/multiline_literals.ty) this IS Tycho's multi-line string form:
         *     s := ("line one\n"
         *           "line two\n")
         * Sound on raw text: escapes are kept as source text here, and every Tycho
         * escape is exactly two characters (\n \t \r \\ \"), so no escape can absorb
         * a byte across the join -- which is also why \0 and \xNN are not in the
         * escape set (see the lexer). An f-string never joins (it is already sugar
         * for a `+` chain): both `f"a" "b"` and `"a" f"b"` stay the two expressions
         * they were and fail exactly as before. */
        char *sv = t->text;
        while (at(ps, TK_STR) && !cur(ps)->ival) { sv = sfmt("%s%s", sv, cur(ps)->text); ps->p++; }
        Expr *e = new_expr(E_STR, t->line);  e->sval = sv; return e;
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
                && nk != TK_KW_U8 && nk != TK_KW_U16 && nk != TK_KW_I8 && nk != TK_KW_I16 && nk != TK_KW_I32 && nk != TK_KW_I64
                && nk != TK_IDENT && nk != TK_LBRACKET && nk != TK_LPAREN && nk != TK_FN) {
                e->ival = T_VOID;            /* the "untyped" marker */
                return e;
            }
            Type elem = parse_type(ps);
            if (at(ps, TK_COLON)) {          /* empty map literal []K: V */
                ps->p++;
                Type val = parse_type(ps);
                Type mt = map_of(elem, val);
                if (mt == T_VOID) die_at(t->line, "map keys must be string, int (directly or through a newtype), a fieldless enum, or a hashable struct/tuple/array");
                e->ival = mt; e->op = TK_COLON;
                return e;
            }
            if (elem == T_VOID)   /* defensive, same as the `[T]` type site (src/tychoc.c:2035): parse_type never yields T_VOID */
                die_at(t->line, "an array element type cannot be void -- every other type is allowed, including bytes, a tuple, a map and Option");
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
            /* The map_* functions were removed in favour of operator/keyword/method
             * syntax. Reject a user-typed call here (the `delete`/`m[k]`/`m.get`
             * desugars build their map_del/map_get nodes directly in resolve,
             * bypassing this parse-time path). */
            if (!strcmp(t->text, "map_set")) die_at(t->line, "map_set was removed; use `m[k] = v`");
            if (!strcmp(t->text, "map_has")) die_at(t->line, "map_has was removed; use `k in m`");
            if (!strcmp(t->text, "map_del")) die_at(t->line, "map_del was removed; use `delete m[k]`");
            if (!strcmp(t->text, "map_get")) die_at(t->line, "map_get was removed; use `m.get(k, default)`");
            Expr *e = new_expr(E_CALL, t->line);
            e->sval = t->text;
            e->pkg  = g_cur_pkg_prefix;   /* the package this call appears in; resolver tries <pkg>name first */
            int cap = 0;
            int any_named = 0;
            while (!at(ps, TK_RPAREN)) {
                if (e->nargs == cap) {
                    cap = cap ? cap * 2 : 4;
                    e->args = (Expr **)xrealloc(e->args, (size_t)cap * sizeof(Expr *));
                    e->argnames = (char **)xrealloc(e->argnames, (size_t)cap * sizeof(char *));
                }
                /* F4: a `name: value` argument is a named struct field. `IDENT ':'`
                 * right here is unambiguous -- no other construct starts that way
                 * inside a call arg (a lambda arg starts with `fn`, a map with `[`). */
                char *aname = NULL;
                if (at(ps, TK_IDENT) && peek(ps, 1)->kind == TK_COLON) {
                    aname = cur(ps)->text;
                    ps->p += 2;   /* consume IDENT and ':' */
                    any_named = 1;
                }
                e->argnames[e->nargs] = aname;
                e->args[e->nargs++] = parse_expr(ps);
                if (!accept(ps, TK_COMMA)) break;
            }
            eat(ps, TK_RPAREN, "')'");
            if (!any_named) e->argnames = NULL;   /* all-positional: match every other call site */
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
    if (at(ps, TK_ELLIPSIS)) {   /* x... : spread into a variadic parameter (validated at the call site) */
        Tok *t = cur(ps); ps->p++;
        Expr *sp = new_expr(E_SPREAD, t->line);
        sp->lhs = e;
        e = sp;
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
    if (at(ps, TK_AMP)) {                  /* &lvalue — an inout argument */
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
static Stmt **parse_arm_inline(Parser *ps, int value, int *count);   /* F7: one-line match-arm body */
static Stmt *parse_stmt(Parser *ps);   /* a value branch may lead with ordinary statements before its tail */

/* `for x in COLL:` desugars to a collection-temp decl plus a range loop. The temp
 * decl must land in the block BEFORE the loop; parse_stmt queues it here and
 * parse_block / parse_value_block drain the queue ahead of the returned statement. */
static Stmt *g_pending[64];
static int g_npending = 0;

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
 * Restricted to TAIL position (RHS of :=/typed-:=/=/place-=/return). A branch/arm
 * is an indented block of zero or more ordinary statements followed by a FINAL
 * value expression, held as a trailing `S_EXPR` (the normal statement grammar
 * rejects a bare non-call expr, so the tail is parsed on its own).
 * The non-decl positions desugar at parse time: the trailing S_EXPR becomes an
 * ordinary S_RETURN/S_ASSIGN/S_INDEXSET/S_FIELDSET (leading statements untouched),
 * so resolve+codegen are reused wholesale; only `:=`/typed-`:=` keeps the control
 * node on S_DECL.ctrl, its tails rewritten to assignments once the inferred type
 * is known (resolver). The leading statements ride along as an ordinary block, so
 * a multi-statement branch lowers to plain sequenced C, no statement-expression. */

/* Is the parser at the LAST logical line of the current value block? The branch
 * tail is a bare value expression on the final line; every earlier line is an
 * ordinary statement. A line that opens its own indented block (its terminating
 * newline is followed by an INDENT) is a statement, never the tail. */
static int value_block_at_tail(Parser *ps) {
    int i = ps->p;
    while (ps->t[i].kind != TK_NEWLINE && ps->t[i].kind != TK_EOF && ps->t[i].kind != TK_DEDENT)
        i++;
    if (ps->t[i].kind != TK_NEWLINE) return 1;   /* DEDENT/EOF with no newline: treat as the tail */
    int j = i + 1;
    while (ps->t[j].kind == TK_NEWLINE) j++;
    if (ps->t[j].kind == TK_INDENT) return 0;    /* this line owns a block body -> a statement */
    return ps->t[j].kind == TK_DEDENT;           /* the tail iff the next line is this block's DEDENT */
}

static Stmt **parse_value_block(Parser *ps, int *count) {
    eat(ps, TK_INDENT, "an indented value branch");
    Stmt **body = NULL; int n = 0, cap = 0;
    while (!at(ps, TK_DEDENT) && !at(ps, TK_EOF)) {
        if (accept(ps, TK_NEWLINE)) continue;
        if (value_block_at_tail(ps)) {
            /* the branch's value: a bare expression on the final line */
            Expr *tail = parse_expr(ps);
            if (!at(ps, TK_NEWLINE))
                die_at(tail->line, "a value branch must end in a value expression");
            Stmt *se = new_stmt(S_EXPR, tail->line);
            se->expr = tail;
            if (n == cap) { cap = cap ? cap * 2 : 8; body = (Stmt **)xrealloc(body, (size_t)cap * sizeof(Stmt *)); }
            body[n++] = se;
            eat(ps, TK_NEWLINE, "newline");
            continue;   /* the tail is last; the next iteration hits the DEDENT and stops */
        }
        /* a leading statement (may queue a foreach collection-temp, drained as in parse_block) */
        Stmt *st = parse_stmt(ps);
        for (int k = 0; k < g_npending; k++) {
            if (n == cap) { cap = cap ? cap * 2 : 8; body = (Stmt **)xrealloc(body, (size_t)cap * sizeof(Stmt *)); }
            body[n++] = g_pending[k];
        }
        g_npending = 0;
        if (n == cap) { cap = cap ? cap * 2 : 8; body = (Stmt **)xrealloc(body, (size_t)cap * sizeof(Stmt *)); }
        body[n++] = st;
    }
    eat(ps, TK_DEDENT, "end of the value branch");
    if (n == 0 || body[n - 1]->kind != S_EXPR)
        die_at(n ? body[n - 1]->line : cur(ps)->line, "a value branch must end in a value expression");
    *count = n;
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
        if (s->narms == cap) { cap = cap ? cap * 2 : 4; s->arms = (MatchArm *)xrealloc(s->arms, (size_t)cap * sizeof(MatchArm)); }
        MatchArm *arm = &s->arms[s->narms++];
        arm->nbinds = 0; arm->sub = NULL; arm->nsubbinds = 0; arm->sub_vi = -1;
        arm->pn = 0;
        TokKind k0 = peek(ps, 0)->kind;
        if (k0 == TK_INT || k0 == TK_CHAR || k0 == TK_TRUE || k0 == TK_FALSE
            || (k0 == TK_MINUS && peek(ps, 1)->kind == TK_INT)
            || (k0 == TK_IDENT && (peek(ps, 1)->kind == TK_DOTDOT || peek(ps, 1)->kind == TK_PIPE))) {
            /* SCALAR pattern arm: a literal, `a..b` (inclusive range), a set
             * `a | b | ...`, or a const NAME element (resolved against the
             * subject's type). `variant` stays NULL; the resolver type-checks
             * and folds const names, and the codegen emits a switch/chain on
             * the values. */
            arm->variant = NULL;
            arm->line = peek(ps, 0)->line; arm->sub_line = arm->line;
            for (;;) {
                if (arm->pn >= 8) die_at(arm->line, "too many values in one match arm (max 8)");
                int kind = 0; int64_t v = 0; int neg = 0;
                if (accept(ps, TK_MINUS)) neg = 1;
                if (at(ps, TK_INT)) { kind = 0; v = peek(ps, 0)->ival; ps->p++; }
                else if (at(ps, TK_CHAR)) { kind = 1; v = peek(ps, 0)->ival; ps->p++; }
                else if (at(ps, TK_TRUE)) { kind = 2; v = 1; ps->p++; }
                else if (at(ps, TK_FALSE)) { kind = 2; v = 0; ps->p++; }
                else if (at(ps, TK_IDENT)) { kind = 3; arm->pcname[arm->pn] = pkg_mangle(peek(ps, 0)->text); ps->p++; }
                else die_at(arm->line, "a match arm must be a variant name, a scalar literal, a range, or `_`");
                if (neg) { if (kind != 0) die_at(arm->line, "'-' is only valid before an int literal in a match arm"); v = -v; }
                arm->pkind[arm->pn] = kind;
                arm->plo[arm->pn] = v;
                arm->phi[arm->pn] = v;
                arm->pcname[arm->pn] = (kind == 3) ? arm->pcname[arm->pn] : NULL;
                arm->pch[arm->pn] = NULL;
                if (accept(ps, TK_DOTDOT)) {          /* inclusive range: `a..b` */
                    int hkind = 0; int64_t h = 0; int hneg = 0;
                    if (accept(ps, TK_MINUS)) hneg = 1;
                    if (at(ps, TK_INT)) { hkind = 0; h = peek(ps, 0)->ival; ps->p++; }
                    else if (at(ps, TK_CHAR)) { hkind = 1; h = peek(ps, 0)->ival; ps->p++; }
                    else if (at(ps, TK_TRUE)) { hkind = 2; h = 1; ps->p++; }
                    else if (at(ps, TK_FALSE)) { hkind = 2; h = 0; ps->p++; }
                    else if (at(ps, TK_IDENT)) { hkind = 3; arm->pch[arm->pn] = pkg_mangle(peek(ps, 0)->text); ps->p++; }
                    else die_at(arm->line, "expected a value after '..' in the match arm");
                    if (hneg) { if (hkind != 0) die_at(arm->line, "'-' is only valid before an int literal in a match arm"); h = -h; }
                    if (hkind != kind) die_at(arm->line, "a range's two ends must be the same kind of value");
                    arm->phi[arm->pn] = h;
                }
                arm->pn++;
                if (!accept(ps, TK_PIPE)) break;
            }
        } else {
            Tok *vn = eat(ps, TK_IDENT, "a match arm `Variant(bindings):` or `Variant:`");
            const char *vqual = NULL, *vname = vn->text;
            if (accept(ps, TK_DOT)) {           /* qualified `pkg.Variant:` */
                vqual = vn->text;
                vname = eat(ps, TK_IDENT, "a variant name after the package qualifier")->text;
            }
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
            arm->line = vn->line; arm->sub_line = vn->line;
            if (accept(ps, TK_LPAREN)) {
            /* One level of NESTED pattern, when the spelling is unambiguous at
             * parse time: `pkg.Variant` (a DOT follows) or `Variant(b, ...)` (an
             * LPAREN follows). A bare `Name` cannot be told from a binding here --
             * it stays in binds[] and the resolver promotes it if the payload's
             * enum has a variant by that name (MatchArm, above). */
            if (at(ps, TK_IDENT) && (peek(ps, 1)->kind == TK_DOT || peek(ps, 1)->kind == TK_LPAREN)) {
                Tok *sn = eat(ps, TK_IDENT, "a nested variant name");
                arm->sub_line = sn->line;
                if (accept(ps, TK_DOT)) {          /* qualified `pkg.Variant` */
                    const char *inm = eat(ps, TK_IDENT, "a variant name after the package qualifier")->text;
                    check_pkg_private(sn->text, inm, sn->line);
                    arm->sub = sfmt("%s%s", pkg_prefix_for(sn->text), inm);
                } else {
                    arm->sub = (char *)sn->text;   /* unqualified: matched by `raw` */
                }
                if (accept(ps, TK_LPAREN)) {       /* the nested variant's own payload bindings */
                    while (!at(ps, TK_RPAREN)) {
                        if (arm->nsubbinds >= 8) die_at(sn->line, "too many bindings (max 8)");
                        arm->subbinds[arm->nsubbinds++] = eat(ps, TK_IDENT, "a binding name")->text;
                        if (!accept(ps, TK_COMMA)) break;
                    }
                    eat(ps, TK_RPAREN, "')' after the nested pattern's bindings");
                }
            } else {
                while (!at(ps, TK_RPAREN)) {
                    if (arm->nbinds >= 8) die_at(vn->line, "too many bindings (max 8)");
                    arm->binds[arm->nbinds++] = eat(ps, TK_IDENT, "a binding name")->text;
                    if (!accept(ps, TK_COMMA)) break;
                }
            }
            eat(ps, TK_RPAREN, "')'");
            }
        }
        eat(ps, TK_COLON, "':' after the arm pattern");
        if (accept(ps, TK_NEWLINE))          /* block arm: indented body on the next line */
            arm->body = value ? parse_value_block(ps, &arm->nbody)
                              : parse_block(ps, &arm->nbody);
        else                                 /* F7: inline arm body — `Some(i): return i` on one line */
            arm->body = parse_arm_inline(ps, value, &arm->nbody);
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

/* DIVERGENCE. A call that never returns: `die(msg)` and `exit(code)`, the two
 * builtins that end the process. Both are typed T_VOID (there is no bottom type),
 * so the value if/match rule "every branch produces a value" would reject them —
 * but a branch that leaves the program produces no value BY DEFINITION, and it is
 * the natural spelling of the failure arm:
 *     srv := match net.listen(h, p):
 *         Ok(fd): fd
 *         Err(e): die("cannot bind")
 * Modelled here, not with a type: a diverging tail is neither rewritten into
 * `name = tail` nor offered to branch unification. It stays the plain statement
 * it already is.
 *
 * The test is SYNTACTIC, and that is sound rather than convenient: `die`/`exit`
 * are registered builtins, and a program that defines either name is rejected
 * outright (`error: 'die' is already defined`, the `dup_check` in register order),
 * so the name cannot mean anything else. `sval` is the written name both before
 * resolution (the parser stores it verbatim) and after (builtins are never
 * mangled — codegen matches `die` on `e->sval` the same way), so one predicate
 * serves the parse-time rewrite and the resolve-time unification. `!qual`
 * excludes `pkg.die` and `!lhs` excludes calling a function VALUE. */
static int expr_diverges(Expr *e) {
    return e && e->kind == E_CALL && !e->qual && !e->lhs && e->sval &&
           (!strcmp(e->sval, "die") || !strcmp(e->sval, "exit"));
}

/* rewrite the single-expr tail of every branch/arm of a value if/match into a
 * concrete statement (S_RETURN / S_ASSIGN / place-set) — the parse-time desugar
 * for the non-declaration tail positions. `kind` is the target StmtKind;
 * `name`/`target` supply the destination. Recurses through elif chains. */
static void ctrl_rewrite_tails(Stmt *c, StmtKind kind, char *name, Expr *target) {
    Stmt **branches[2]; int bn[2]; int nbr = 0;
    if (c->kind == S_IF) {
        branches[nbr] = c->body; bn[nbr] = c->nbody; nbr++;
        if (c->nels == 1 && c->els[0]->kind == S_IF) { ctrl_rewrite_tails(c->els[0], kind, name, target); }
        else { branches[nbr] = c->els; bn[nbr] = c->nels; nbr++; }
    }
    /* for a match, each arm is a branch */
    int narm = (c->kind == S_MATCH) ? c->narms : 0;
    for (int i = 0; i < nbr + narm; i++) {
        Stmt **body = (i < nbr) ? branches[i] : c->arms[i - nbr].body;
        int nb = (i < nbr) ? bn[i] : c->arms[i - nbr].nbody;
        Stmt *se = body[nb - 1];                     /* the S_EXPR(tail) — the LAST statement of the branch */
        if (expr_diverges(se->expr)) continue;        /* die()/exit(): no value to place; leave the statement alone */
        Stmt *ns = new_stmt(kind, se->line);
        if (kind == S_RETURN)      { ns->expr = se->expr; }
        else if (kind == S_ASSIGN) { ns->name = name; ns->expr = se->expr; }
        else                       { ns->target = target; ns->expr = se->expr; }  /* S_INDEXSET / S_FIELDSET */
        body[nb - 1] = ns;                           /* leading statements ride along untouched */
    }
}

/* collect the (already-resolved) tail expression of every branch/arm — used to
 * unify their types for a `:=` value if/match. Mirrors ctrl_rewrite_tails,
 * INCLUDING its divergence skip: a `die()`/`exit()` tail contributes no type, so
 * the two stay in lockstep about which tails carry a value. */
static void ctrl_tail_push(Expr **out, int *n, Expr *e) {
    if (!expr_diverges(e)) out[(*n)++] = e;
}
static void ctrl_collect_tails(Stmt *c, Expr **out, int *n) {
    if (c->kind == S_IF) {
        ctrl_tail_push(out, n, c->body[c->nbody - 1]->expr);   /* the tail is the LAST statement of the branch */
        if (c->nels == 1 && c->els[0]->kind == S_IF) ctrl_collect_tails(c->els[0], out, n);
        else ctrl_tail_push(out, n, c->els[c->nels - 1]->expr);
    } else {   /* S_MATCH */
        for (int i = 0; i < c->narms; i++) ctrl_tail_push(out, n, c->arms[i].body[c->arms[i].nbody - 1]->expr);
    }
}

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

/* Bind `sub` to a fresh temp queued in g_pending (emitted just before the
 * assignment) and return an ident that reads it. */
static Expr *hoist_place_leg(Expr *sub, int line) {
    if (g_npending >= 64) die_at(line, "too many hoisted index expressions in one statement (max 64)");
    Stmt *d = new_stmt(S_DECL, line);
    d->name = sfmt("_cx%d", g_forin_uid++);
    d->expr = sub;
    g_pending[g_npending++] = d;
    Expr *tv = new_expr(E_IDENT, line);
    tv->sval = d->name; tv->pkg = g_cur_pkg_prefix;
    return tv;
}

/* A compound assignment `a[i] OP= e` evaluates the place TWICE (read, then
 * store), so a side-effecting index `i` would run twice. Bind each index in the
 * place that CONTAINS A CALL to a fresh temp (queued in g_pending, emitted just
 * before the assignment) and rewrite the place to use it, so the index runs
 * once. Pure indices are untouched, so the common `a[i] += e` is byte-identical.
 *
 * `compound` also hoists the ARGUMENTS of a user subscript in the place
 * (`g.edge(f()).weight += 1`, spec §13.4 single-evaluation). A subscript call is
 * inlined to its yielded place at resolve time, so its argument ends up inside
 * the place expression and, without this, is emitted twice — the read took
 * nodes[0] and the store went to nodes[1]. Only the compound path needs it: a
 * plain `place = rhs` evaluates the place once, and leaving it alone keeps the
 * emitted C for that form unchanged. */
static void hoist_index_calls(Expr *place, int line, int compound) {
    Expr *chain[32]; int nc = 0;
    for (Expr *cur = place; cur; ) {
        if (cur->kind == E_INDEX || (compound && cur->kind == E_CALL)) {
            if (nc >= 32) die_at(line, "assignment place too deeply nested (max 32 indices)");
            chain[nc++] = cur;
        }
        if (cur->kind == E_INDEX || cur->kind == E_FIELD || cur->kind == E_TUPIDX) cur = cur->lhs;
        /* a subscript call in the place: `g.edge(i)` keeps its receiver in ->qual
         * (a bare ident -- no legs under it), `a.b.edge(i)` in ->lhs->lhs. */
        else if (compound && cur->kind == E_CALL)
            cur = (cur->lhs && cur->lhs->kind == E_FIELD) ? cur->lhs->lhs : NULL;
        else break;
    }
    for (int i = nc - 1; i >= 0; i--) {   /* innermost leg (evaluated first) hoisted first */
        Expr *ix = chain[i];
        if (ix->kind == E_CALL) {
            for (int a = 0; a < ix->nargs; a++)
                if (expr_has_call(ix->args[a])) ix->args[a] = hoist_place_leg(ix->args[a], line);
        } else if (ix->rhs && expr_has_call(ix->rhs)) {
            ix->rhs = hoist_place_leg(ix->rhs, line);
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
            arm->sub = NULL; arm->nsubbinds = 0; arm->sub_line = an->line; arm->sub_vi = -1;
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
        Stmt *s = parse_stmt(ps);        /* parses the for (`0..<N`, or foreach: deferred for type-directed lowering) */
        if (s->kind != S_FORRANGE)
            die_at(t->line, "parallel supports `for i in 0..<N` and `for x in collection` loops only");
        s->parallel = 1;
        return s;
    }
    if (t->kind == TK_FOR) {
        ps->p++;
        int par_here = g_parallel_ctx; g_parallel_ctx = 0;   /* only THIS for is parallel; nested fors below are not */
        /* bare `for:` -- an infinite loop, exited by `break` or `return`. It is
         * the condition form with a literal `true`, which is the same node
         * resolve_parfor already builds for the channel-drain worker, so
         * break/continue, the loop arena and wl_check (which skips a constant
         * condition) all need no new case. */
        if (at(ps, TK_COLON)) {
            ps->p++;
            eat(ps, TK_NEWLINE, "newline");
            Stmt *s = new_stmt(S_WHILE, t->line);
            Expr *tru = new_expr(E_BOOL, t->line); tru->ival = 1;
            s->expr = tru;
            s->body = parse_block(ps, &s->nbody);
            return s;
        }
        /* three-clause `for init; cond; post:`.
         *
         * A top-level `;` on the header line is the ONLY thing that tells this
         * form from `for C:`, and it is unambiguous because `;` has no other
         * grammar anywhere in the language (tests/reject/semi_no_grammar.ty
         * keeps the C-style statement terminator illegal). So: scan the header
         * to end of line, tracking () and [] depth, and record the two `;` and
         * the LAST top-level `:` -- last, because a typed init (`i: int = 0`)
         * puts a colon of its own ahead of the block's.
         *
         * ALL THREE CLAUSES ARE REQUIRED; `for:` is the only degenerate form.
         * That is not a rule imposed on the grammar, it is what the grammar
         * already says: there is no empty-statement production in this parser
         * (parse_stmt on a NEWLINE is an error), so `for ; c; p:` has nothing to
         * parse in the init slot, and an empty condition would have no `bool` to
         * check. Each is refused by name below rather than by whatever parse_stmt
         * happened to say. */
        {
            int i_semi1 = -1, i_semi2 = -1, i_colon = -1, depth = 0;
            for (int k = 0; ; k++) {
                TokKind kk = peek(ps, k)->kind;
                if (kk == TK_NEWLINE || kk == TK_EOF) break;
                if (kk == TK_LPAREN || kk == TK_LBRACKET) depth++;
                else if (kk == TK_RPAREN || kk == TK_RBRACKET) depth--;
                else if (depth == 0 && kk == TK_SEMI) {
                    if (i_semi1 < 0) i_semi1 = k;
                    else if (i_semi2 < 0) i_semi2 = k;
                    else die_at(t->line, "a three-clause `for` has exactly three clauses: `for init; cond; post:`");
                } else if (depth == 0 && kk == TK_COLON) i_colon = k;
            }
            if (i_semi1 >= 0) {
                if (i_semi2 < 0)
                    die_at(t->line, "a three-clause `for` is `for init; cond; post:` -- two ';' separating three clauses");
                if (i_colon < i_semi2)
                    die_at(t->line, "expected ':' before the block");
                if (i_semi1 == 0)
                    die_at(t->line, "the init clause is required -- write `for:` for an infinite loop");
                if (i_semi2 == i_semi1 + 1)
                    die_at(t->line, "the condition clause is required -- write `for:` for an infinite loop");
                if (i_colon == i_semi2 + 1)
                    die_at(t->line, "the post clause is required -- write `for cond:` for a loop that advances in its body");
                /* Rewrite the first ';' and the block ':' to NEWLINE so the init
                 * and post clauses parse through parse_stmt itself. That is the
                 * point: `:=`, a typed decl, `=` and every compound `op=` form
                 * come along for free, instead of a second hand-written copy of
                 * the assignment grammar that could drift from the first. */
                peek(ps, i_semi1)->kind = TK_NEWLINE;
                peek(ps, i_colon)->kind = TK_NEWLINE;
                Stmt *init = parse_stmt(ps);
                if (init->kind != S_DECL && init->kind != S_ASSIGN)
                    die_at(init->line, "the init clause of a three-clause `for` is a declaration or an assignment");
                Expr *cond = parse_expr(ps);
                eat(ps, TK_SEMI, "';' after the condition");
                Stmt *post = parse_stmt(ps);
                if (post->kind != S_ASSIGN)
                    die_at(post->line, "the post clause of a three-clause `for` is an assignment to a variable (`i += 1`)");
                eat(ps, TK_NEWLINE, "newline");
                Stmt *s = new_stmt(S_FOR3, t->line);
                s->expr = cond;
                s->els = (Stmt **)xmalloc(sizeof(Stmt *));
                s->els[0] = init; s->nels = 1;
                int nb = 0; Stmt **ub = parse_block(ps, &nb);
                s->body = (Stmt **)xmalloc((size_t)(nb + 1) * sizeof(Stmt *));
                for (int k = 0; k < nb; k++) s->body[k] = ub[k];
                s->body[nb] = post;       /* post is the last body statement: it runs every iteration */
                s->nbody = nb + 1;
                return s;
            }
        }
        /* counting form `parallel for i in 0..<N:` or foreach `for x in COLL:` */
        if (at(ps, TK_IDENT) && peek(ps, 1)->kind == TK_IN) {
            Tok *var = eat(ps, TK_IDENT, "a loop variable");
            eat(ps, TK_IN, "'in'");
            /* `0..<N` -- the counting spelling for `parallel for`, and ONLY for
             * `parallel for`. The runtime chunks a known iteration space across
             * K = tycho_ncpu() tasks (gen_parfor, src/tychoc.c:9932), and a
             * three-clause loop's post clause is arbitrary code, so its iteration
             * count is not knowable in advance and cannot be chunked. A
             * SEQUENTIAL `for i in 0..<N:` is refused deliberately: accepting it
             * would make `0..<N` into `range()` under a new name and leave the
             * three-clause form with nothing to do (plan.md's Pre-flight).
             * Implicit step 1, ascending, exclusive upper bound -- so unlike
             * `range()` there is no zero-step case to diagnose at all, and N is
             * an ordinary expression, evaluated once at the spawn site.
             *
             * The header is SCANNED for a top-level `..<` (same shape as the `;`
             * scan above) rather than tested at peek(1), so `for i in (a+b)..<n:`
             * and a sequential `for i in 0..<3:` both reach the diagnostic that
             * fits them instead of "expected ':' before the block". */
            {
                int i_dotlt = -1, depth = 0;
                for (int k = 0; ; k++) {
                    TokKind kk = peek(ps, k)->kind;
                    if (kk == TK_NEWLINE || kk == TK_EOF) break;
                    if (kk == TK_LPAREN || kk == TK_LBRACKET) depth++;
                    else if (kk == TK_RPAREN || kk == TK_RBRACKET) depth--;
                    else if (depth == 0 && kk == TK_DOTLT) { i_dotlt = k; break; }
                }
                if (i_dotlt >= 0) {
                    if (!par_here)
                        die_at(t->line, "`0..<N` counts only in a `parallel for` -- write `for %s := 0; %s < N; %s += 1:` for a sequential count",
                               var->text, var->text, var->text);
                    if (i_dotlt != 1 || !at(ps, TK_INT) || cur(ps)->ival != 0)
                        die_at(t->line, "`parallel for` counts from zero: write `0..<N` -- a literal `0`, then `..<`, then the exclusive upper bound");
                    ps->p += 2;                  /* the `0` and the `..<` */
                    Stmt *s = new_stmt(S_FORRANGE, t->line);
                    s->name = var->text;
                    Expr *zero = new_expr(E_INT, t->line); zero->ival = 0;
                    s->r_start = zero; s->r_stop = parse_expr(ps);
                    eat(ps, TK_COLON, "':' before the block");
                    eat(ps, TK_NEWLINE, "newline");
                    s->body = parse_block(ps, &s->nbody);
                    return s;   /* caller (TK_PARALLEL) sets s->parallel = 1 */
                }
            }
            /* HISTORY: `for i in range(a, b, step):` was the counting form until
             * 2026-07-29, when it was replaced by the three-clause `for` and, for
             * `parallel for`, by `0..<N`. `range` was never a procedure -- it was
             * recognised only here, by lexeme, in a `for` header (see parse_fn's
             * note on contextual identifiers), so deleting this branch deletes the
             * whole feature. Without the branch below the header would fall
             * through to foreach and die with "unknown procedure 'range'" at
             * resolve, which names neither replacement; the refusal is kept
             * explicit so the diagnostic can be copy-pasted. */
            if (at(ps, TK_IDENT) && !strcmp(cur(ps)->text, "range") && peek(ps, 1)->kind == TK_LPAREN)
                die_at(t->line, "`range()` was removed: write `for %s := 0; %s < N; %s += 1:` to count, or `parallel for %s in 0..<N:` to count in parallel",
                       var->text, var->text, var->text, var->text);
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
                fe->r_start = coll; fe->r_stop = NULL;
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
            fr->r_start = zero; fr->r_stop = lenc;
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
        /* E_CALL is allowed as a target only if it resolves to a user subscript's place
         * (checked in resolve_stmt, which corrects the kind); a plain call is rejected there. */
        if (e->kind != E_INDEX && e->kind != E_FIELD && e->kind != E_TUPIDX && e->kind != E_CALL)
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
        /* A5 (evaluation order): pin left-to-right — a side-effecting index in the
         * place runs BEFORE the RHS. Without this the index and RHS are emitted
         * inline and the C compiler picks the order (tychoc got RHS-first), which
         * diverged from tychoc0 on `arr[f()] = g()`. Hoisting the index into a
         * pending temp sequences it first, matching tychoc0. Pure indices untouched. */
        hoist_index_calls(e, t->line, 0);
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
            hoist_index_calls(e, t->line, 1);   /* single-eval a side-effecting index / subscript argument */
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
    if (e->kind == E_IDENT && !strcmp(e->sval, "while"))   /* F9: `while cond:` — Tycho has no while keyword */
        die_at(t->line, "Tycho has no `while` -- use `for cond:` for a conditional loop (e.g. `for i < 3:`)");
    /* `spawn f(x)` alone reaches here as an E_SPAWN, and the generic message below
     * never states the rule it broke: a Task handle is affine and MUST NOT be
     * discarded (spec §21), and binding it is what gives the implicit join at scope
     * exit something to wait on. Say that instead of "no effect" -- the task very
     * much would have had one. */
    if (e->kind == E_SPAWN)
        die_at(t->line, "a `spawn` must be bound to a task handle -- write `t := spawn f(args)`, because the implicit join at scope exit needs a handle to wait on (a Task cannot be discarded)");
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

/* F7: a one-line match-arm body (`Some(i): return i` / value `Some(i): i*2`),
 * used when the arm pattern's `:` is NOT followed by a newline. Statement form
 * parses a single statement (draining any foreach collection-temp it queues, as
 * parse_block does); value form parses a single expression. The arm loop's
 * top-of-iteration `accept(NEWLINE)` absorbs the end-of-line token. */
static Stmt **parse_arm_inline(Parser *ps, int value, int *count) {
    if (value) {
        Expr *tail = parse_expr(ps);
        Stmt *se = new_stmt(S_EXPR, tail->line);
        se->expr = tail;
        Stmt **body = (Stmt **)xmalloc(sizeof(Stmt *));
        body[0] = se; *count = 1;
        return body;
    }
    Stmt *st = parse_stmt(ps);
    Stmt **body = NULL; int n = 0, cap = 0;
    for (int k = 0; k < g_npending; k++) {   /* a foreach queued a collection-temp decl to emit first */
        if (n == cap) { cap = cap ? cap * 2 : 4; body = (Stmt **)xrealloc(body, (size_t)cap * sizeof(Stmt *)); }
        body[n++] = g_pending[k];
    }
    g_npending = 0;
    if (n == cap) { cap = cap ? cap * 2 : 4; body = (Stmt **)xrealloc(body, (size_t)cap * sizeof(Stmt *)); }
    body[n++] = st;
    *count = n;
    return body;
}

static Proc *parse_fn(Parser *ps) {
    g_ncur_typarams = 0;                  /* fresh `$T` scope for this function */
    g_ncur_sizeparams = 0;                /* fresh `$N` size-param scope (const generics 1.6B) */
    eat(ps, TK_FN, "'fn'");
    /* `fn handle(conn: int):` is a real trip-up -- `handle` is reserved (§3.6)
     * but "expected a procedure name" never said WHY, so it reads as a parser
     * bug. Name the keyword. A reserved word is exactly a token whose lexeme
     * `keyword()` maps back to its own kind; the contextual identifiers of §3.7
     * (`package`, `extern`, `soa`, `sink`, `where`, `range`, every builtin, ...)
     * lex as TK_IDENT and are unaffected -- they stay legal procedure names. */
    if (!at(ps, TK_IDENT) && cur(ps)->text && keyword(cur(ps)->text) == cur(ps)->kind) {
        g_err_col = cur(ps)->col;
        die_at(cur(ps)->line, "'%s' is a reserved keyword and cannot be used as a procedure name", cur(ps)->text);
    }
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
        /* PROTOTYPE: `name: sink type` — an owned, mutable parameter. Contextual
         * keyword (not reserved). The callee owns the argument and may mutate it
         * in place; the caller's argument is consumed (adopted if it is a movable
         * dead local / fresh value, else copied — see arg_into at the call site). */
        int is_sink = 0;
        if (!is_inout && at(ps, TK_IDENT) && !strcmp(cur(ps)->text, "sink")) { ps->p++; is_sink = 1; }
        /* variadic: `name: ...T` — the param is a `[T]`; a call packs its trailing args. */
        int is_variadic = accept(ps, TK_ELLIPSIS);
        if (is_variadic && (is_inout || is_sink))
            die_at(pn->line, "a variadic parameter cannot also be inout or sink");
        Type pt = parse_type(ps);
        if (is_variadic) pt = arr_of(pt);      /* `...T` -> the param's type is [T] */
        if (pr->nparams == cap) { cap = cap ? cap * 2 : 4; pr->params = (Param *)xrealloc(pr->params, (size_t)cap * sizeof(Param)); }
        pr->params[pr->nparams].name = pn->text;
        pr->params[pr->nparams].type = pt;
        pr->params[pr->nparams].is_inout = is_inout;
        pr->params[pr->nparams].is_sink = is_sink;
        pr->params[pr->nparams].is_variadic = is_variadic;
        pr->params[pr->nparams].ffi_ct = NULL;
        pr->nparams++;
        if (!accept(ps, TK_COMMA)) break;
    }
    eat(ps, TK_RPAREN, "')'");
    for (int i = 0; i + 1 < pr->nparams; i++)   /* only the LAST parameter may be variadic */
        if (pr->params[i].is_variadic)
            die_at(pr->line, "a variadic parameter must be the last parameter of '%s'", nameT->text);

    if (accept(ps, TK_ARROW)) { pr->ret = parse_type(ps); pr->has_ret = 1; }
    else pr->ret = T_VOID;
    if (IS_HANDLE(pr->ret))   /* FFI R2: a handle is freed at its owner's scope exit; returning it (the only way is from an extern opener) would free-then-escape */
        die_at(pr->line, "a Tycho fn cannot return a handle -- only an `extern fn` opener may; a handle is freed at the end of its scope and cannot escape it");
    pr->generic = (g_ncur_typarams > 0 || g_ncur_sizeparams > 0);   /* a `$T` type param OR a `$N` size param in the signature makes this a template */
    pr->ntyparams = g_ncur_typarams;       /* record the $-params in order, for explicit call-site type args */
    for (int i = 0; i < g_ncur_typarams; i++) pr->typarams[i] = typaram_of(g_cur_typarams[i]);
    pr->nsizeparams = g_ncur_sizeparams;   /* const generics 1.6B: record the signature's `$N` size params (bound per instance) */
    for (int i = 0; i < g_ncur_sizeparams; i++) pr->sizeparams[i] = g_cur_sizeparams[i];

    /* generics: optional `where pred(T), pred2(T2)` -- a fixed compiler-known
     * predicate set, checked at instantiation against the inferred concrete type. */
    if (cur(ps)->kind == TK_IDENT && !strcmp(cur(ps)->text, "where")) {
        if (!pr->generic) die_at(cur(ps)->line, "`where` constraints require a generic function (one with a `$T` parameter)");
        ps->p++;
        for (;;) {
            if (pr->ncon >= 8) die_at(cur(ps)->line, "at most 8 `where` constraints per function");
            Tok *pt = eat(ps, TK_IDENT, "a `where` predicate (numeric/comparable/has_str/hashable/defaultable) or a type parameter");
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
                if (strcmp(pt->text, "numeric") && strcmp(pt->text, "comparable") && strcmp(pt->text, "has_str") && strcmp(pt->text, "hashable") && strcmp(pt->text, "defaultable"))
                    die_at(pt->line, "unknown `where` predicate '%s' (known: numeric, comparable, has_str, hashable, defaultable -- or use a type set, `T: int | float`)", pt->text);
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
    g_ncur_sizeparams = 0;                /* leave the function's `$N` size-param scope */
    return pr;
}

/* the base identifier of a place spine (v / v.f / v[i] / v.f[i].g), or NULL if the
 * root is not a plain variable (a call/literal root is not a projectable place). */
static const char *place_root_name(Expr *e) {
    while (e && (e->kind == E_FIELD || e->kind == E_INDEX)) e = e->lhs;
    return (e && e->kind == E_IDENT) ? e->sval : NULL;
}
/* how many times identifier `name` is referenced anywhere in `e`. */
static int ident_use_count(Expr *e, const char *name) {
    if (!e) return 0;
    int n = (e->kind == E_IDENT && e->sval && !strcmp(e->sval, name)) ? 1 : 0;
    n += ident_use_count(e->lhs, name) + ident_use_count(e->rhs, name);
    for (int i = 0; i < e->nargs; i++) n += ident_use_count(e->args[i], name);
    return n;
}

/* subscript name(p: T, ...) -> inout U:      (2.4: a user-defined projection)
 *     yield &<place>
 * Parses + structurally validates; the place's TYPE is checked against U in resolve. */
static void parse_subscript(Parser *ps) {
    ps->p++;   /* consume the `subscript` contextual keyword (dispatch verified its text) */
    Tok *nameT = eat(ps, TK_IDENT, "a subscript name");
    eat(ps, TK_LPAREN, "'(' after the subscript name");
    Subscript sub; memset(&sub, 0, sizeof sub);
    sub.name = nameT->text;   /* matched by bare name + receiver type at the call site (v1) */
    sub.line = nameT->line;
    int cap = 0;
    while (!at(ps, TK_RPAREN)) {
        Tok *pn = eat(ps, TK_IDENT, "a parameter name");
        eat(ps, TK_COLON, "':' after a parameter name");
        Type pt = parse_type(ps);
        if (sub.nparams == cap) { cap = cap ? cap * 2 : 4; sub.params = (Param *)xrealloc(sub.params, (size_t)cap * sizeof(Param)); }
        sub.params[sub.nparams].name = pn->text;
        sub.params[sub.nparams].type = pt;
        sub.params[sub.nparams].is_inout = 0;
        sub.params[sub.nparams].is_sink = 0;
        sub.params[sub.nparams].is_variadic = 0;
        sub.params[sub.nparams].ffi_ct = NULL;
        sub.nparams++;
        if (!accept(ps, TK_COMMA)) break;
    }
    eat(ps, TK_RPAREN, "')'");
    eat(ps, TK_ARROW, "'-> inout <type>' -- a subscript yields an inout projection");
    if (!accept(ps, TK_INOUT))
        die_at(cur(ps)->line, "a subscript must yield an inout projection: `-> inout <type>`");
    sub.ret = parse_type(ps);
    eat(ps, TK_COLON, "':' before the yield body");
    eat(ps, TK_NEWLINE, "newline");
    eat(ps, TK_INDENT, "an indented `yield &<place>` body");
    while (accept(ps, TK_NEWLINE)) { }
    if (!(at(ps, TK_IDENT) && !strcmp(cur(ps)->text, "yield")))
        die_at(cur(ps)->line, "a subscript body must be a single `yield &<place>`");
    ps->p++;   /* consume `yield` */
    Expr *y = parse_expr(ps);
    if (y->kind != E_ADDR)
        die_at(sub.line, "a subscript must yield a place: `yield &<place>` (e.g. `yield &g.nodes[i]`)");
    sub.place = y->lhs;
    while (accept(ps, TK_NEWLINE)) { }
    eat(ps, TK_DEDENT, "a subscript body is a single `yield` line");
    /* the place must be rooted in a parameter (else the projection dangles) ... */
    const char *root = place_root_name(sub.place);
    int is_param = 0;
    for (int i = 0; i < sub.nparams; i++) if (root && !strcmp(sub.params[i].name, root)) is_param = 1;
    if (!is_param)
        die_at(sub.line, "a subscript must yield a place rooted in one of its parameters (else the projection would dangle)");
    /* ... and no parameter may appear more than once (no argument double-evaluation). */
    for (int i = 0; i < sub.nparams; i++)
        if (ident_use_count(sub.place, sub.params[i].name) > 1)
            die_at(sub.line, "subscript parameter '%s' is used more than once in the yielded place (v1: at most once)", sub.params[i].name);
    if (g_nsubs == g_nsubs_cap) { g_nsubs_cap = g_nsubs_cap ? g_nsubs_cap * 2 : 8; g_subs = (Subscript *)xrealloc(g_subs, (size_t)g_nsubs_cap * sizeof(Subscript)); }
    g_subs[g_nsubs++] = sub;
}

/* Substitute a subscript's parameters with the actual call arguments in a CLONE of its
 * yielded place (the template is shared across calls, so it must not be mutated). Each
 * parameter appears at most once (enforced at parse), so an actual arg expr is spliced
 * in directly (used exactly once) — never double-evaluated. */
static Expr *subst_place(Expr *e, Subscript *sub, Expr **actual) {
    if (!e) return NULL;
    if (e->kind == E_IDENT && e->sval)
        for (int i = 0; i < sub->nparams; i++)
            if (!strcmp(sub->params[i].name, e->sval)) return actual[i];
    Expr *c = new_expr(e->kind, e->line);
    *c = *e;
    c->lhs = subst_place(e->lhs, sub, actual);
    c->rhs = subst_place(e->rhs, sub, actual);
    if (e->nargs) {
        c->args = (Expr **)xmalloc((size_t)e->nargs * sizeof(Expr *));
        for (int i = 0; i < e->nargs; i++) c->args[i] = subst_place(e->args[i], sub, actual);
    }
    return c;
}

/* a subscript callable as `recv.name(<nargs> args)`: bare name matches, the declared
 * receiver (first) parameter type == recv, and one parameter per arg plus the receiver. */
static Subscript *find_subscript(const char *name, Type recv, int nargs) {
    for (int i = 0; i < g_nsubs; i++) {
        Subscript *s = &g_subs[i];
        if (!strcmp(s->name, name) && s->nparams == nargs + 1 && s->params[0].type == recv) return s;
    }
    return NULL;
}

/* FFI Stage 1: only scalars + string may cross the C boundary. Composite types
 * (arrays/maps/structs/Option/Result/tuples/fn) have tycho-internal C reps, not a
 * stable C ABI — reject them (fail closed). void is allowed as a return only. */
static int ffi_scalar_type(Type t) {
    return t == T_INT || t == T_CHAR || t == T_FLOAT || t == T_BOOL || t == T_STRING || t == T_PTR ||
           t == T_F32 || is_sized_int(t);   /* first-class sized numerics cross as their real C type */
}
/* A scalar array (`[int]`/`[float]`) crosses the FFI as a `(const T*, long)` pair,
 * exactly like `bytes` crosses as (ptr,len). Returns the C element-pointer type, or
 * NULL for a non-scalar-array (arrays of string/struct/nested stay rejected — they
 * have no flat, self-describing C ABI). */
static const char *ffi_arr_ptr_ctype(Type t) {
    if (t == T_ARRAY_INT)   return "const tycho_int *";
    if (t == T_ARRAY_FLOAT) return "const double *";
    return NULL;
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
        int p_inout = accept(ps, TK_INOUT);   /* FFI R4: `name: inout T` = an out / in-out param — the C fn writes through a T* */
        /* FFI-boundary sized int (u8/u16/.../i64): `int` to Tycho, sized C type at the ABI.
         * Only for a by-value param (a sized `inout` out-param isn't supported yet). */
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
            if (!ffi_scalar_type(pt) || pt == T_STRING) die_at(pn->line, "extern fn '%s': an `inout` (out) parameter '%s' must be int/char/float/bool/ptr — string/bytes/handle/composite have no trivial out-param ABI", pr->name, pn->text);
        } else if (!ffi_scalar_type(pt) && pt != T_BYTES && !ffi_arr_ptr_ctype(pt) && !IS_HANDLE(pt)) {
            die_at(pn->line, "extern fn '%s': parameter '%s' must be int/char/float/bool/string/ptr/bytes, [int]/[float], or a handle (no other composites across the C boundary)", pr->name, pn->text);
        }
        if (pr->nparams == cap) { cap = cap ? cap * 2 : 4; pr->params = (Param *)xrealloc(pr->params, (size_t)cap * sizeof(Param)); }
        pr->params[pr->nparams].name = pn->text;
        pr->params[pr->nparams].type = pt;
        pr->params[pr->nparams].is_inout = p_inout;
        pr->params[pr->nparams].is_sink = 0;   /* extern params are never sink */
        pr->params[pr->nparams].is_variadic = 0;   /* nor variadic (FFI variadics is a non-goal) */
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
        if (!ffi_scalar_type(pr->ret) && pr->ret != T_BYTES && !ffi_arr_ptr_ctype(pr->ret) && !IS_HANDLE(pr->ret) && !ret_opt_str) die_at(pr->line, "extern fn '%s': return type must be int/char/float/bool/string/ptr/bytes, [int]/[float], Option(string), a handle, or omitted", pr->name);
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
 * owning variable's scope exit. */
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
    Type _tp[TYCHO_MAX_TYPARAMS]; int _ntp = 0;  /* `struct Box($T, $U)` type parameters */
    if (accept(ps, TK_LPAREN)) {
        while (!at(ps, TK_RPAREN)) {
            Type tp = parse_type(ps);            /* `$T` registers the name + returns its typaram type; a bare field `T` then refers to it */
            if (!IS_TYPARAM(tp)) die_at(nameT->line, "a struct type parameter must be written `$Name`");
            if (_ntp >= TYCHO_MAX_TYPARAMS) die_at(nameT->line, "too many struct type parameters (max 16)");
            _tp[_ntp++] = tp;
            if (!accept(ps, TK_COMMA)) break;
        }
        eat(ps, TK_RPAREN, "')' after the struct type parameters");
    }
    /* check the MANGLED name (like the enum site): a cross-package collision
     * ("a__b" + "c" vs "a" + "b__c") otherwise slips through to a duplicate C
     * typedef and fails at cc with no tycho-level diagnostic.
     * `handle_find` belongs here too (14-ffi.md §25): a handle shares the ONE
     * type namespace with struct/enum/newtype, and parse_handle already tests
     * all four (:3616). Omitting it here made the rule one-directional --
     * `struct H` after `handle H` was accepted and RAN on tychoc while
     * `handle H` after `struct H` was rejected. */
    if (struct_find(pkg_mangle(nameT->text)) >= 0 || enum_find(pkg_mangle(nameT->text)) >= 0
        || newtype_find(pkg_mangle(nameT->text)) >= 0 || handle_find(pkg_mangle(nameT->text)) >= 0)
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
    Type _tp[TYCHO_MAX_TYPARAMS]; int _ntp = 0;  /* `enum Tree($T, $U)` type parameters */
    if (accept(ps, TK_LPAREN)) {
        while (!at(ps, TK_RPAREN)) {
            Type tp = parse_type(ps);            /* `$T` registers the name + returns its typaram type; a bare payload `T` then refers to it */
            if (!IS_TYPARAM(tp)) die_at(nameT->line, "an enum type parameter must be written `$Name`");
            if (_ntp >= TYCHO_MAX_TYPARAMS) die_at(nameT->line, "too many enum type parameters (max 16)");
            _tp[_ntp++] = tp;
            if (!accept(ps, TK_COMMA)) break;
        }
        eat(ps, TK_RPAREN, "')' after the enum type parameters");
    }
    /* `handle_find` included for the same reason as the struct site above: one
     * type namespace, checked symmetrically (14-ffi.md §25). */
    if (struct_find(pkg_mangle(nameT->text)) >= 0 || enum_find(pkg_mangle(nameT->text)) >= 0
        || newtype_find(pkg_mangle(nameT->text)) >= 0 || handle_find(pkg_mangle(nameT->text)) >= 0)
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
        var->raw = (char *)vn->text;   /* as written, for an unqualified nested pattern */
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
    /* `handle_find` included for the same reason as the struct site above: one
     * type namespace, checked symmetrically (14-ffi.md §25). */
    if (struct_find(pkg_mangle(nameT->text)) >= 0 || enum_find(pkg_mangle(nameT->text)) >= 0
        || newtype_find(pkg_mangle(nameT->text)) >= 0 || handle_find(pkg_mangle(nameT->text)) >= 0)
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
    if (e->op == TK_PLUS && a->kind == E_STR && b->kind == E_STR) {
        /* `const TERM = "\r\n" + "\r\n"` folds to one literal, so a header terminator
         * is a literal and not two allocations per loop iteration. Same raw-text
         * soundness argument as the adjacent-literal join in parse_primary. */
        Expr *n = new_expr(E_STR, e->line); n->sval = sfmt("%s%s", a->sval, b->sval); return n;
    }
    if (a->kind != E_INT || b->kind != E_INT) return e;   /* int-only const arithmetic */
    /* HOST WIDTH: every fold arm runs in fixed 64-bit, never in `long`. tycho `int` is
     * 64-bit two's-complement by spec, so a compiler hosted on an ILP32 machine (where
     * `long` is 32 bits) must still fold `1 << 40` to 1099511627776, not truncate it. */
    int64_t x = a->ival, y = b->ival, r;
    switch (e->op) {
        case TK_PLUS:    r = x + y; break;
        case TK_MINUS:   r = x - y; break;
        case TK_STAR:    r = x * y; break;
        case TK_SLASH:   if (y == 0) die_at(e->line, "const expression divides by zero"); r = x / y; break;
        case TK_PERCENT: if (y == 0) die_at(e->line, "const expression divides by zero"); r = x % y; break;
        case TK_AMP:     r = x & y; break;
        case TK_PIPE:    r = x | y; break;
        case TK_CARET:   r = x ^ y; break;
        case TK_SHL:     if (y < 0) die_at(e->line, "const expression shifts by a negative count");
                         r = y >= 64 ? 0 : (int64_t)((uint64_t)x << y); break;
        case TK_SHR:     if (y < 0) die_at(e->line, "const expression shifts by a negative count");
                         r = y >= 64 ? 0 : x >> y; break;
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
        if (at(&ps, TK_IDENT) && !strcmp(cur(&ps)->text, "subscript")) { parse_subscript(&ps); continue; }
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
    int         inout[16];   /* per-param: is it an inout (by-pointer) param? */
    int         sink[16];  /* per-param: is it a `sink` (owned, mutable) param? (prototype) */
    int         variadic[16];  /* per-param: is it a variadic `...T` param (only the last may be)? */
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
/* The pkg-config NAMES are retained separately from the flags they resolve to,
 * and the two answer different questions. `g_pkgdeps` is what goes on the cc
 * line; it is the *resolved* form, and on a host where the library is absent it
 * is EMPTY -- indistinguishable from a package that declared no deps at all. A
 * harness that wants to SKIP rather than fail needs the question "which packages
 * does this program require?", which only the names answer, and it needs the
 * answer on exactly the hosts where resolution fails. So the name is recorded
 * BEFORE resolution is attempted and regardless of whether it succeeds. Read out
 * by `--print-deps`; see the flag's comment in main. */
static const char **g_pkgnames;
static int  g_npkgnames = 0, g_pkgnames_cap = 0;
/* Set by `--print-deps`: the caller wants the names, so resolving each one would
 * fork pkg-config for an answer nobody reads, and on a host missing the library
 * it would print a "could not resolve" line into the exact case the caller is
 * asking in order to handle gracefully. */
static int g_pkgdeps_names_only = 0;
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
        char *name = sfmt("%s", s);
        int seen = 0;
        for (int i = 0; i < g_npkgnames; i++) if (!strcmp(g_pkgnames[i], name)) { seen = 1; break; }
        if (!seen) { TBL_ENSURE(g_pkgnames, g_npkgnames, g_pkgnames_cap); g_pkgnames[g_npkgnames++] = name; }
        if (g_pkgdeps_names_only) continue;
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
    Type *b = new_binds();
    return match_type(gt->params[0].type, recv, b) ? gt->name : NULL;
}
/* Stage-2 generics: each instance carries its OWN cloned body — a deep copy of
 * the template body with `$T` substituted at clone time — so instances resolve
 * independently with no shared/sticky resolved state (the source of the prior
 * multi-instantiation, typed-local, and nested-call bugs). */
typedef struct { Proc *tmpl; char *name; Type params[16]; int nparams; Type ret; Type *binds; Stmt **body; int nbody;
                 int64_t spvals[16]; int nsp; } GInst;   /* const generics 1.6B: this instance's `$N` size-param values (names from tmpl->sizeparams) */
static GInst *g_ginsts; static int g_nginsts = 0, g_nginsts_cap = 0;
static Stmt **clone_block(Stmt **body, int n, Type *binds);   /* per-instance body clone; defined near ginst_to_proc */
static Proc **g_inst_procs; static int g_ninst_procs = 0, g_inst_procs_cap = 0;   /* resolved generic-instance Procs, shared by the prototype + body emit loops (Stage-2 #3) */
static void instantiate_generic(Proc *gt, Expr *e);   /* defined after resolve_expr */
static char *type_mangle_ident(Type t);               /* C-identifier-safe spelling of a type */

static void register_builtins(void) {
    /* designated initializers: robust to field order (inout[] sits between
     * params and nparams). All builtins are by-value (no inout). */
    TBL_RESERVE(g_sigs, 64, g_sigs_cap);   /* must exceed the builtin count below (run of appends w/o per-line ENSURE) */
    g_sigs[g_nsigs++] = (Sig){ .name="print",  .ret=T_VOID,         .params={ T_STRING },                .nparams=1, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="println",.ret=T_VOID,         .params={ T_STRING },                .nparams=1, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="eprint", .ret=T_VOID,         .params={ T_STRING },                .nparams=1, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="input",  .ret=T_STRING,       .params={ 0 },                       .nparams=0, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="read_all",.ret=T_STRING,      .params={ 0 },                       .nparams=0, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="clock",  .ret=T_INT,          .params={ 0 },                       .nparams=0, .builtin=1 };   /* monotonic nanoseconds */
    g_sigs[g_nsigs++] = (Sig){ .name="now",    .ret=T_INT,          .params={ 0 },                       .nparams=0, .builtin=1 };   /* wall-clock UNIX seconds */
    g_sigs[g_nsigs++] = (Sig){ .name="ncpu",   .ret=T_INT,          .params={ 0 },                       .nparams=0, .builtin=1 };   /* worker count = parallel-for fan-out width */
    g_sigs[g_nsigs++] = (Sig){ .name="chr",    .ret=T_STRING,       .params={ T_INT },                   .nparams=1, .builtin=1 };   g_sigs[g_nsigs++] = (Sig){ .name="to_char",.ret=T_CHAR, .params={ T_INT }, .nparams=1, .builtin=1 };   /* int -> byte, the two shapes: chr wraps it in a one-byte string, to_char keeps it as a `char`. Same 0..255 abort. */
    g_sigs[g_nsigs++] = (Sig){ .name="die",    .ret=T_VOID,         .params={ T_STRING },                .nparams=1, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="exit",   .ret=T_VOID,         .params={ T_INT },                   .nparams=1, .builtin=1 };   /* terminate with an explicit status; die() is exit(1) with a message. Diverging (expr_diverges) */
    g_sigs[g_nsigs++] = (Sig){ .name="str",    .ret=T_STRING,       .params={ T_INT },                   .nparams=1, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="substr", .ret=T_STRING,       .params={ T_STRING, T_INT, T_INT },  .nparams=3, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="find",   .ret=T_INT,          .params={ T_STRING, T_STRING },      .nparams=2, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="char_at",.ret=T_CHAR,         .params={ T_STRING, T_INT },         .nparams=2, .builtin=1 };   /* s[i] as a `char` (same bounds abort); s[i] itself still yields int */
    g_sigs[g_nsigs++] = (Sig){ .name="split",  .ret=T_ARRAY_STRING, .params={ T_STRING, T_STRING },      .nparams=2, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="read_file",.ret=T_STRING,     .params={ T_STRING },                .nparams=1, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="write_file",.ret=T_BOOL,      .params={ T_STRING, T_STRING },      .nparams=2, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="list_dir",.ret=T_ARRAY_STRING, .params={ T_STRING },               .nparams=1, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="args",   .ret=T_ARRAY_STRING, .params={ 0 },                       .nparams=0, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="getenv", .ret=T_STRING,       .params={ T_STRING },                .nparams=1, .builtin=1 };
    g_sigs[g_nsigs++] = (Sig){ .name="is_null",.ret=T_BOOL,         .params={ T_PTR },                   .nparams=1, .builtin=1 };   /* FFI: opaque-handle NULL test */
    g_sigs[g_nsigs++] = (Sig){ .name="to_ptr", .ret=T_PTR,          .params={ T_INT },                   .nparams=1, .builtin=1 };   /* FFI: int -> opaque ptr (sentinel pointers like SQLITE_TRANSIENT = (void*)-1) */
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

/* closest EXPORTED symbol of an already-imported package to a mistyped `name`
 * (F8 discoverability): the package's fns are in g_sigs mangled `pkg__real`, so
 * compare against the unprefixed tail. `qual` is the source qualifier. */
static char *pkg_prefix_for(const char *qualifier);   /* defined below the import table */
static const char *suggest_pkg_symbol(const char *qual, const char *name) {
    char *prefix = pkg_prefix_for(qual);              /* e.g. "strings__" */
    size_t pl = strlen(prefix), nl = strlen(name);
    const char *best = NULL; int bestd = 99;
    const char *rel = NULL;                            /* related name: one is a prefix of the other (to_uppercase ~ to_upper) */
    for (int i = 0; i < g_nsigs; i++) {
        if (strncmp(g_sigs[i].name, prefix, pl) || !g_sigs[i].name[pl]) continue;
        const char *s = g_sigs[i].name + pl;
        dym(name, s, &best, &bestd);
        size_t sl = strlen(s);
        if (!rel && sl >= 4 && nl >= 4 &&
            ((nl >= sl && !strncmp(name, s, sl)) || (sl >= nl && !strncmp(s, name, nl))))
            rel = s;
    }
    const char *lv = dym_pick(name, best, bestd);
    return lv ? lv : rel;
}

/* -------- corelib discoverability (F8): point an unknown bare call at the stdlib.
 * The unknown-procedure path is terminal (about to die), so we can afford to scan
 * the corelib for a package or exported function matching the name the user
 * reached for -- turning `sort(xs)` -> core:sort and `upper(s)` -> core:strings.
 * to_upper. Fail-safe: any misstep returns NULL and the caller's generic error
 * stands. Only a col-0 `fn NAME(` (a top-level export) is considered; a leading
 * underscore is package-private and skipped. */
static const char *corelib_root(void);   /* defined below */
static char *read_file(const char *path);   /* defined near main */

static char *cl_line_fn(const char *ln) {   /* the exported fn a `fn NAME(` line names, else NULL */
    if (strncmp(ln, "fn ", 3)) return NULL;
    const char *p = ln + 3;
    while (*p == ' ') p++;
    const char *s = p;
    while (isalnum((unsigned char)*p) || *p == '_') p++;
    if (p == s || *p != '(') return NULL;
    return xstrndup(s, (size_t)(p - s));
}
static char *cl_first_fn(const char *pdir) {   /* an example exported fn from a package dir, or NULL */
    DIR *pd = opendir(pdir);
    if (!pd) return NULL;
    struct dirent *fe; char *found = NULL;
    while (!found && (fe = readdir(pd)) != NULL) {
        const char *f = fe->d_name; size_t L = strlen(f);
        if (L < 4 || strcmp(f + L - 3, ".ty")) continue;
        FILE *fh = fopen(sfmt("%s/%s", pdir, f), "rb");
        if (!fh) continue;
        char line[512];
        while (fgets(line, sizeof line, fh)) {
            char *fn = cl_line_fn(line);
            if (fn && fn[0] != '_') { found = fn; break; }
        }
        fclose(fh);
    }
    closedir(pd);
    return found;
}
/* On return, *strong is 1 for a high-confidence match (an exact/`_`-suffix fn or a
 * package name) that should outrank a weak local typo, 0 for a Levenshtein-only
 * guess (which a local user-fn/builtin suggestion should win over). */
static const char *corelib_hint(const char *name, int *strong) {
    *strong = 0;
    const char *root = corelib_root();
    if (!root) return NULL;
    DIR *d = opendir(root);
    if (!d) return NULL;
    const char *fn_best = NULL, *fn_pkg = NULL; int fn_rank = 99;   /* 0 exact, 1 `_`-suffix (upper->to_upper) */
    const char *lv_best = NULL, *lv_pkg = NULL; int lv_d = 99;      /* Levenshtein pool (typos) */
    char *pkg_match = NULL;                                          /* the name IS a package */
    struct dirent *de; int npkg = 0;
    while ((de = readdir(d)) != NULL && npkg < 256) {
        const char *pk = de->d_name;
        if (pk[0] == '.') continue;
        char *pdir = sfmt("%s/%s", root, pk);
        if (!dir_exists(pdir)) continue;
        npkg++;
        if (!strcmp(pk, name)) pkg_match = xstrndup(pk, strlen(pk));
        DIR *pd = opendir(pdir);
        if (!pd) continue;
        struct dirent *fe; int nf = 0;
        while ((fe = readdir(pd)) != NULL && nf < 32) {
            const char *f = fe->d_name; size_t L = strlen(f);
            if (L < 4 || strcmp(f + L - 3, ".ty")) continue;
            nf++;
            FILE *fh = fopen(sfmt("%s/%s", pdir, f), "rb");
            if (!fh) continue;
            char line[512];
            while (fgets(line, sizeof line, fh)) {
                char *fn = cl_line_fn(line);
                if (!fn || fn[0] == '_') continue;
                size_t nl = strlen(name), fl = strlen(fn);
                int rank = 99;
                if (!strcmp(fn, name)) rank = 0;
                else if (fl > nl + 1 && !strcmp(fn + fl - nl, name) && fn[fl - nl - 1] == '_') rank = 1;
                if (rank < fn_rank) { fn_rank = rank; fn_best = fn; fn_pkg = pkg_match && !strcmp(pk,name) ? pkg_match : xstrndup(pk, strlen(pk)); }
                int ed = edit_dist(name, fn);
                if (ed < lv_d) { lv_d = ed; lv_best = fn; lv_pkg = xstrndup(pk, strlen(pk)); }
            }
            fclose(fh);
        }
        closedir(pd);
    }
    closedir(d);
    /* priority: an exact/`_`-suffix fn match, then the name-is-a-package case,
     * then a close typo of some fn. (Leaks a few strings on the die path -- fine.) */
    if (fn_best && fn_rank <= 1) {
        *strong = 1;
        return sfmt("core:%s provides `%s` -- add `import \"core:%s\"` and call `%s.%s(...)`",
                    fn_pkg, fn_best, fn_pkg, fn_pkg, fn_best);
    }
    if (pkg_match) {
        *strong = 1;
        char *ex = cl_first_fn(sfmt("%s/%s", root, pkg_match));
        if (ex) return sfmt("`%s` is a corelib package -- add `import \"core:%s\"` and call e.g. `%s.%s(...)`",
                            pkg_match, pkg_match, pkg_match, ex);
        return sfmt("`%s` is a corelib package -- add `import \"core:%s\"` and qualify its functions as `%s.NAME(...)`",
                    pkg_match, pkg_match, pkg_match);
    }
    if (lv_best && lv_d <= (strlen(name) <= 4 ? 1 : 2))   /* weak: a typo of some corelib fn */
        return sfmt("core:%s has `%s` -- add `import \"core:%s\"` and call `%s.%s(...)`",
                    lv_pkg, lv_best, lv_pkg, lv_pkg, lv_best);
    return NULL;
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
 * first arg) set it to 1 around that one resolve. #2 (docs/guides/map-mutation.md). */
static int g_place = 0;
static int g_in_arg = 0;   /* set while resolving a call argument: the one place `&` is legal */
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
static int block_ends_in_return(Stmt **body, int n);       /* fwd: fall-off-the-end lint (defined near codegen) */

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
/* A type that cannot stand as the type of a value: `void` (a proc call used as
 * an expression), a bare `None`, a bare `Ok`/`Err`, a still-pending decl. */
static int uninferrable(Type t) {
    return t == T_VOID || t == T_NONE || t == T_OK_PARTIAL || t == T_ERR_PARTIAL || t == T_PENDING;
}
static void pend_ground(const char *name, Type t, int line) {
    /* The composition matters, not only the top tag: `push(xs, nop())` composes
     * `arr_of(void)` and hands THAT to pend_ground, so a bare `t == T_VOID` test
     * passed it through and a `void` element reached codegen as a `void`
     * function parameter ("'void' must be the only parameter and unnamed").
     * The same holds for `map_set(m, k, nop())` -> [K: void]. */
    if (uninferrable(t)
        || (is_array(t) && uninferrable(arr_elem(t)))
        || (is_map(t) && (uninferrable(map_key(t)) || uninferrable(map_val(t)))))
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
/* Builtins callable with UFCS method syntax `recv.name(args)` -> `name(recv, args)`.
 * Only receiver-first builtins are eligible; constructors (Some/Ok/Err/None), env/IO
 * niladics (args/clock/now/ncpu/getenv/input/read_*) and effectful non-methods are
 * excluded. The desugar re-resolves through the SAME builtin arg-shape checking the
 * direct-call form uses, so a wrong receiver type still fails closed there.
 * Kept byte-identical with tychoc0.ty's is_ufcs_builtin. */
static int is_ufcs_builtin(const char *n) {
    if (!n) return 0;
    static const char *bs[] = { "str", "substr", "chr", "split", "keys", "find", "char_at", "len",
        "push", "pop", "reserve", "map_get", "map_has", "map_set", "map_del",
        "sqrt", "pow", "floor", "fabs", "to_float", "to_int", "to_str", "to_bool",
        "to_bytes", "to_ptr", "to_u8", "to_u16", "to_u32", "to_u64",
        "to_i8", "to_i16", "to_i32", "to_i64", "to_f32", "is_null", 0 };
    for (int i = 0; bs[i]; i++) if (!strcmp(n, bs[i])) return 1;
    return 0;
}
/* An int/float LITERAL adapting to an f32 destination, on every route that does it.
 * The lexer's DBL_MAX test (lex_num) cannot catch this one: 3.5e38 is a perfectly
 * ordinary binary64, and it only becomes an infinity here, when the destination
 * narrows it to binary32 -- correct source, wrong number, nothing on stderr. */
static void f32_lit(Expr *e) {
    if (e->kind == E_INT) { e->kind = E_FLOAT; e->fval = (double)e->ival; }
    if (e->fval > FLT_MAX || e->fval < -FLT_MAX)
        die_at(e->line, "f32 literal out of range: `%g` exceeds the largest f32 "
                        "(IEEE-754 binary32); write to_f32(1.0/0.0) for an infinity", e->fval);
    e->type = T_F32;
}
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
        case E_SPREAD:   /* a variadic call unwraps its spread args before resolving them; anywhere else is a misuse */
            die_at(e->line, "spread `...` is only valid as the argument to a variadic parameter");
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
                if (s->inout[i])
                    die_at(e->line, "cannot spawn a function with inout parameters (no shared state across threads)");
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
                    caps[ncap].name = (char *)ids[i]; caps[ncap].type = vt; caps[ncap].is_inout = 0; caps[ncap].is_sink = 0; caps[ncap].is_variadic = 0; caps[ncap].ffi_ct = NULL;
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
            int saved_arg = g_in_arg; g_in_arg = 0;   /* a `&` in the body is not an argument of the call that resolved this lambda */
            resolve_block(pr->body, pr->nbody, pr->ret);
            g_in_arg = saved_arg;
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
                die_at(e->line, "tuple index .%lld on a non-tuple value (%s)", (long long)e->ival, type_name(bt));
            if (e->ival < 0 || e->ival >= tup_n(bt))
                die_at(e->line, "tuple index %lld out of range (the tuple has %d elements)", (long long)e->ival, tup_n(bt));
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
            /* precedence: local var -> const -> variant -> fn. Look the const up
             * under the SAME package-qualified key parse_const registered it with
             * (pkg_mangle). At resolve time g_cur_pkg_prefix is already reset to "",
             * so use the per-expr e->pkg (stamped at parse) -- else an imported
             * package's `const K` (stored "<pkg>K") is missed here and misresolves
             * as an unknown variable. Main has an empty prefix -> the bare name. */
            Expr *clit = consts_find((e->pkg && e->pkg[0]) ? sfmt("%s%s", e->pkg, e->sval) : e->sval);
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
                    if (fs->inout[i]) die_at(e->line, "'%s' has an inout parameter, so it can't be a function value", e->sval);
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
                    die_at(e->line, "map keys must be string, int (directly or through a newtype), a fieldless enum, or a hashable struct/tuple/array");
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
            if (elem == T_VOID)   /* REACHABLE, unlike the two parse sites: the first element is a call to a procedure with no return type (tests/reject/arr_elem_void.ty) */
                die_at(e->line, "an array element cannot be void -- this element produces no value (a call to a procedure that returns nothing?)");
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
            /* A string byte and a bytes byte are the SAME read: both are the
             * length-headered char* buffer (T_BYTES at :498), so both lower to
             * tycho_str_get. The result is the byte VALUE as an int (0..255),
             * not a 1-length buffer: that is what a byte-classifying loop
             * (`if is_ctl(b[i])`) wants, it needs no allocation, and it keeps
             * `b[i]` and `s[i]` from meaning different things for one repr. */
            if (bt == T_STRING || bt == T_BYTES) return e->type = T_INT;
            die_at(e->line, "can only index an array, a string, bytes, or a map (as a place)");
        }
        case E_SLICE: {   /* xs[a:b] — a sub-range of the same array/soa type; s[a:b] -> a substring */
            Type bt = resolve_expr(e->lhs);
            if (e->rhs && resolve_expr(e->rhs) != T_INT)
                die_at(e->line, "slice start must be int");
            if (e->nargs && resolve_expr(e->args[0]) != T_INT)
                die_at(e->line, "slice end must be int");
            if (bt == T_STRING) return e->type = T_STRING;   /* a string slice is a fresh substring */
            if (bt == T_BYTES) return e->type = T_BYTES;     /* bytes: same substr, stays bytes */
            if (IS_BOUNDED(bt))   /* a slice would need a .data view; bounded stores inline */
                die_at(e->line, "cannot slice a bounded[...] value; copy it into a [%s] first", type_name(arr_elem(bt)));
            if (!is_array(bt) && !IS_SOA(bt))
                die_at(e->line, "can only slice an array, soa, a string, or bytes");
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
                /* e->lhs MUST be cleared: this node is now an E_CALL, and the E_CALL arm
                 * treats a non-NULL lhs as a call-on-expression (an indirect call through a
                 * fn VALUE) and resolves it. A second resolve of this node -- which happens
                 * for every argument of a generic call, a generic struct literal or a generic
                 * enum payload, all of which resolve their arguments once to infer `$T` and
                 * again against the bound type -- would then resolve the package ident `net`
                 * as a variable and die with "unknown variable 'net'". */
                e->kind = E_CALL; e->sval = q; e->op = TK_ENUM; e->ival = evi; e->nargs = 0;
                e->lhs = NULL; e->pkg_done = 1;
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
            const char *fsg = suggest_field(sd, e->sval);
            if (fsg) die_at(e->line, "struct %s has no field '%s'; did you mean '%s'?", sd->name, e->sval, fsg);
            die_at(e->line, "struct %s has no field '%s'", sd->name, e->sval);
        }
        case E_ADDR:   /* &place; only valid as the direct argument of an inout
                        * parameter (the call site validates that). Anywhere
                        * else -- `r := &a`, `&a + 1` -- it would emit a C
                        * initializer taken from a pointer, which is invalid C
                        * (the design-aggregate-ref finding). Reject it here. */
            if (!g_in_arg)
                die_at(e->line, "'&' is only valid as the argument to an inout parameter, e.g. f(&x)");
            return e->type = resolve_expr(e->lhs);
        case E_STRUCTLIT:   /* produced by resolving E_CALL; already typed */
            return e->type;
        case E_CALL: {
            /* zero$(T): the defaultable-type zero value, for seeding a generic
             * accumulator (`total := zero$(T)`) that must work on an empty input
             * without seeding from xs[0]. Lowered here to the scalar zero LITERAL
             * (int 0 / float 0.0 / "" / false), so codegen needs no case. v1
             * accepts exactly the four scalar-zero types (matches the
             * `defaultable(T)` predicate); a non-defaultable type arg fails closed.
             * Guarded on ntypeargs so a user call `zero(x)` without `$(...)` is
             * untouched. */
            if (e->sval && !strcmp(e->sval, "zero") && e->ntypeargs > 0) {
                if (e->nargs != 0) die_at(e->line, "zero$(T) takes no value arguments");
                if (e->ntypeargs != 1) die_at(e->line, "zero$(T) takes exactly one type argument");
                Type zt = e->typeargs[0];
                Expr *z = scalar_zero_expr(zt, e->line);
                if (!z) die_at(e->line, "zero$(%s): only int, float, bool, and string are defaultable", type_name(zt));
                *e = *z;
                return e->type = zt;
            }
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
                  /* m.get(k) / m.get(k, default): a map read spelled as a method on a
                   * map receiver. Rewrite to the m[k] rvalue path (no default) or the
                   * internal map_get node (explicit default) -- reusing the exact same
                   * lowering, so emitted C is unchanged. Receiver-directed: `get` on any
                   * non-map type falls through to the UFCS resolution below untouched. */
                  if (is_map(_qvt) && !strcmp(e->sval, "get")) {
                      if (e->nargs != 1 && e->nargs != 2)
                          die_at(e->line, "m.get(key) or m.get(key, default) takes one or two arguments (got %d)", e->nargs);
                      Expr *recv = new_expr(E_IDENT, e->line); recv->sval = (char *)e->qual; recv->pkg = e->pkg;
                      if (e->nargs == 1) {   /* m.get(k) == m[k] rvalue read (value-type zero on a miss) */
                          e->kind = E_INDEX; e->lhs = recv; e->rhs = e->args[0];
                          e->args = NULL; e->nargs = 0; e->qual = NULL; e->sval = NULL;
                      } else {               /* m.get(k, d) == map_get(m, k, d) */
                          Expr **na = (Expr **)xmalloc(3 * sizeof(Expr *));
                          na[0] = recv; na[1] = e->args[0]; na[2] = e->args[1];
                          e->args = na; e->nargs = 3; e->qual = NULL; e->sval = "map_get";
                      }
                      return resolve_expr(e);
                  }
                  /* g.edge(i) where `edge` is a user subscript (2.4) on g's type: inline
                   * its yielded place with the receiver + args substituted, then resolve
                   * THAT — a place in a place context, an rvalue read otherwise. No call. */
                  { Subscript *sub = find_subscript(e->sval, _qvt, e->nargs);
                    if (sub) {
                        Expr **actual = (Expr **)xmalloc((size_t)sub->nparams * sizeof(Expr *));
                        Expr *recv = new_expr(E_IDENT, e->line); recv->sval = (char *)e->qual; recv->pkg = e->pkg;
                        actual[0] = recv;
                        for (int i = 0; i < e->nargs; i++) actual[i + 1] = e->args[i];
                        Type want = sub->ret;
                        *e = *subst_place(sub->place, sub, actual);
                        Type got = resolve_expr(e);
                        if (got != want)
                            die_at(e->line, "subscript '%s' is declared `-> inout %s` but yields a place of type %s",
                                   sub->name, type_name(want), type_name(got));
                        return got;
                    } }
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
                      if (c1 && !c1->builtin && c1->nparams >= 1 && c1->params[0] == _qvt && !c1->inout[0]) ms = c1;
                      if (!ms && e->pkg && e->pkg[0]) {
                          Sig *c2 = sig_find(sfmt("%s%s", e->pkg, e->sval));
                          if (c2 && !c2->builtin && c2->nparams >= 1 && c2->params[0] == _qvt && !c2->inout[0]) ms = c2;
                      }
                      if (!ms) {   /* method defined in the receiver type's package: geom__Circle -> geom__area */
                          const char *pp = type_pkg_prefix(_qvt);
                          if (pp) {
                              Sig *c3 = sig_find(sfmt("%s%s", pp, e->sval));
                              if (c3 && !c3->builtin && c3->nparams >= 1 && c3->params[0] == _qvt && !c3->inout[0]) ms = c3;
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
                  } else if (!fnfield && is_ufcs_builtin(e->sval)) {   /* UFCS on a builtin: x.split(s) -> split(x, s); keep sval, let the builtin dispatch below arg-check it */
                      Expr *recv = new_expr(E_IDENT, e->line); recv->sval = (char *)e->qual; recv->pkg = e->pkg;
                      Expr **na = (Expr **)xmalloc((size_t)(e->nargs + 1) * sizeof(Expr *));
                      na[0] = recv;
                      for (int i = 0; i < e->nargs; i++) na[i + 1] = e->args[i];
                      e->args = na; e->nargs += 1;
                      e->qual = NULL;
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
                /* base.get(k[, default]) on a map receiver: same map-read rewrite as the
                 * local-var path above, for a chained receiver (a.f().get(k)). */
                if (is_map(bt) && !strcmp(fld->sval, "get")) {
                    if (e->nargs != 1 && e->nargs != 2)
                        die_at(e->line, "m.get(key) or m.get(key, default) takes one or two arguments (got %d)", e->nargs);
                    if (e->nargs == 1) {
                        e->kind = E_INDEX; e->lhs = fld->lhs; e->rhs = e->args[0];
                        e->args = NULL; e->nargs = 0; e->sval = NULL;
                    } else {
                        Expr **na = (Expr **)xmalloc(3 * sizeof(Expr *));
                        na[0] = fld->lhs; na[1] = e->args[0]; na[2] = e->args[1];
                        e->args = na; e->nargs = 3; e->sval = "map_get"; e->lhs = NULL;
                    }
                    return resolve_expr(e);
                }
                /* base.edge(i): a user subscript on a chained receiver (a.f().edge(i)). */
                { Subscript *sub = find_subscript(fld->sval, bt, e->nargs);
                  if (sub) {
                      Expr **actual = (Expr **)xmalloc((size_t)sub->nparams * sizeof(Expr *));
                      actual[0] = fld->lhs;
                      for (int i = 0; i < e->nargs; i++) actual[i + 1] = e->args[i];
                      Type want = sub->ret;
                      *e = *subst_place(sub->place, sub, actual);
                      Type got = resolve_expr(e);
                      if (got != want)
                          die_at(e->line, "subscript '%s' is declared `-> inout %s` but yields a place of type %s",
                                 sub->name, type_name(want), type_name(got));
                      return got;
                  } }
                int fnfield = 0;
                if (IS_STRUCT(bt)) {
                    StructDef *sd = &g_structs[STRUCT_ID(bt)];
                    for (int i = 0; i < sd->nfields; i++)
                        if (!strcmp(sd->fields[i].name, fld->sval) && IS_FUNC(sd->fields[i].type)) { fnfield = 1; break; }
                }
                Sig *ms = NULL;
                if (!fnfield) {
                    Sig *c1 = sig_find(fld->sval);
                    if (c1 && !c1->builtin && c1->nparams >= 1 && c1->params[0] == bt && !c1->inout[0]) ms = c1;
                    if (!ms && e->pkg && e->pkg[0]) {
                        Sig *c2 = sig_find(sfmt("%s%s", e->pkg, fld->sval));
                        if (c2 && !c2->builtin && c2->nparams >= 1 && c2->params[0] == bt && !c2->inout[0]) ms = c2;
                    }
                    if (!ms) {   /* method defined in the receiver type's package */
                        const char *pp = type_pkg_prefix(bt);
                        if (pp) {
                            Sig *c3 = sig_find(sfmt("%s%s", pp, fld->sval));
                            if (c3 && !c3->builtin && c3->nparams >= 1 && c3->params[0] == bt && !c3->inout[0]) ms = c3;
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
                } else if (!fnfield && is_ufcs_builtin(fld->sval)) {   /* UFCS builtin on a chained receiver: a.f().len() -> len(a.f()) */
                    Expr **na = (Expr **)xmalloc((size_t)(e->nargs + 1) * sizeof(Expr *));
                    na[0] = fld->lhs;
                    for (int i = 0; i < e->nargs; i++) na[i + 1] = e->args[i];
                    e->args = na; e->nargs += 1;
                    e->sval = (char *)fld->sval;
                    e->lhs = NULL;
                }
            }
            if (e->lhs) {   /* call-on-expression: an indirect call on a fn VALUE (array elem, struct field, call result) */
                Type ct = resolve_exp(e->lhs, T_VOID);
                if (!IS_FUNC(ct)) die_at(e->line, "calling a value that isn't a function (%s)", type_name(ct));
                if (e->nargs != func_n(ct))
                    die_at(e->line, "this function value expects %d argument(s), got %d", func_n(ct), e->nargs);
                for (int i = 0; i < e->nargs; i++) {
                    g_in_arg++;
                    int arg_ok = resolve_exp(e->args[i], func_param(ct, i)) == func_param(ct, i);
                    g_in_arg--;
                    if (!arg_ok) die_at(e->line, "argument %d must be %s", i + 1, type_name(func_param(ct, i)));
                }
                e->op = TK_FN;   /* indirect-call marker */
                return e->type = func_ret(ct);
            }
            Type fvt;   /* indirect call through a function-typed local variable: f(args) */
            if (!e->qual && vars_find(e->sval, &fvt) && IS_FUNC(fvt)) {
                if (e->nargs != func_n(fvt))
                    die_at(e->line, "'%s' expects %d argument(s), got %d", e->sval, func_n(fvt), e->nargs);
                for (int i = 0; i < e->nargs; i++) {
                    g_in_arg++;
                    int arg_ok = resolve_exp(e->args[i], func_param(fvt, i)) == func_param(fvt, i);
                    g_in_arg--;
                    if (!arg_ok) die_at(e->line, "argument %d to '%s' must be %s", i + 1, e->sval, type_name(func_param(fvt, i)));
                }
                e->op = TK_FN;   /* indirect-call marker; gen_call's user-proc tail emits h_<var>(arena, args) */
                return e->type = func_ret(fvt);
            }
            /* Package resolution (Stage B): rewrite e->sval to the package-mangled
             * name before any lookup. An explicit `pkg.name` (e->qual) MUST resolve
             * in that package; an implicit name in an imported package (e->pkg) tries
             * its own package first, else falls through to builtins/unprefixed. */
            if (e->pkg_done) {
                /* already package-resolved by an earlier pass over this same node -- e->sval
                 * is the mangled name and must not be prefixed a second time (see Expr.pkg_done). */
            } else if (e->qual) {
                check_pkg_private(e->qual, e->sval, e->line);
                int _vi;
                char *q = sfmt("%s%s", pkg_prefix_for(e->qual), e->sval);
                if (sig_find(q) || struct_find(q) >= 0 || newtype_find(q) >= 0 || variant_find(q, &_vi) >= 0)
                    { e->sval = q; e->pkg_done = 1; }
                else if (generic_find(q))     /* a generic template lives in the generics registry, not a Sig */
                    { e->sval = q; e->qual = NULL; e->pkg_done = 1; }   /* adopt the mangled name + drop qual so the generic dispatch below instantiates it */
                else {
                    const char *sg = suggest_pkg_symbol(e->qual, e->sval);   /* F8: did-you-mean within the package */
                    if (sg) die_at(e->line, "package '%s' has no symbol '%s'; did you mean '%s'?", e->qual, e->sval, sg);
                    die_at(e->line, "package '%s' has no symbol '%s'", e->qual, e->sval);
                }
            } else if (e->pkg && e->pkg[0]) {
                int _vi;
                char *q = sfmt("%s%s", e->pkg, e->sval);
                if (sig_find(q) || struct_find(q) >= 0 || newtype_find(q) >= 0 || variant_find(q, &_vi) >= 0)
                    { e->sval = q; e->pkg_done = 1; }
                else if (generic_find(q))      /* a package-local generic: rewrite so the generic dispatch below instantiates it */
                    { e->sval = q; e->pkg_done = 1; }
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
            /* F4: named field construction `P(x: 1, y: 2)` -- validate and reorder
             * into positional field order; the positional binding below then
             * type-checks it exactly like `P(1, 2)`. Works for generic structs too
             * (field names are independent of the type arguments). */
            {
                int has_named = 0;
                if (e->argnames) for (int i = 0; i < e->nargs; i++) if (e->argnames[i]) { has_named = 1; break; }
                if (has_named) {
                    if (sid < 0)
                        die_at(e->line, "named arguments are only valid for struct construction");
                    StructDef *nsd = &g_structs[sid];
                    for (int i = 0; i < e->nargs; i++) {
                        if (!e->argnames[i])
                            die_at(e->line, "%s: cannot mix named and positional fields", nsd->name);
                        int ok = 0;
                        for (int f = 0; f < nsd->nfields; f++)
                            if (!strcmp(e->argnames[i], nsd->fields[f].name)) { ok = 1; break; }
                        if (!ok)
                            die_at(e->line, "%s has no field '%s'", nsd->name, e->argnames[i]);
                    }
                    if (e->nargs != nsd->nfields)
                        die_at(e->line, "%s: named construction must give all %d field(s), got %d",
                               nsd->name, nsd->nfields, e->nargs);
                    Expr **ord = (Expr **)xmalloc((size_t)nsd->nfields * sizeof(Expr *));
                    for (int f = 0; f < nsd->nfields; f++) {
                        int found = -1;
                        for (int i = 0; i < e->nargs; i++)
                            if (!strcmp(e->argnames[i], nsd->fields[f].name)) {
                                if (found >= 0) die_at(e->line, "%s: field '%s' given more than once", nsd->name, nsd->fields[f].name);
                                found = i;
                            }
                        ord[f] = e->args[found];
                    }
                    e->args = ord;
                    e->nargs = nsd->nfields;
                    e->argnames = NULL;
                }
            }
            if (sid >= 0 && g_structs[sid].generic) {   /* generic struct: infer the type args from the field values */
                StructDef *t = &g_structs[sid];
                if (e->nargs != t->nfields)
                    die_at(e->line, "%s takes %d field value(s), got %d", t->name, t->nfields, e->nargs);
                Type *binds = new_binds();
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
                    Type *binds = new_binds();
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
                Type at_ = resolve_expr(e->args[0]);
                Type b = base_of(at_);   /* sees through a newtype */
                int scalar_ok = b == T_INT || b == T_BOOL || b == T_FLOAT || b == T_STRING ||
                                b == T_CHAR || b == T_BYTES || is_sized_int(b) || b == T_F32;
                /* F5: aggregates stringify recursively via a generated tycho_str_* helper.
                 * fn/soa/handle/channel/task are not renderable at the top level. */
                int aggr_ok = is_array(b) || is_map(b) || IS_STRUCT(b) || IS_ENUM(b) ||
                              IS_TUP(b) || IS_OPT(b) || IS_RES(b);
                if (!scalar_ok && !aggr_ok)
                    die_at(e->line, "str(x) can't stringify a %s", type_name(at_));
                return e->type = T_STRING;
            }
            /* hash(x): a generic hash over any hashable value — the map-key set,
             * exactly what `where hashable(T)` accepts (int, string, newtypes over
             * them, fieldless enums, composites of hashable leaves; a bare
             * float/bool/char/bytes is NOT a legal map key). Codegen reuses the
             * map's per-type emitter (gen_hash), so equal-by-== values hash equal
             * by construction; per-process seeded like map keys. Returns the map's
             * full 64-bit hash as a signed int. Composite arg types are recorded
             * so their hash functions get emitted (hash_keyused). */
            if (!strcmp(e->sval, "hash")) {
                if (e->nargs != 1) die_at(e->line, "hash(x) takes one argument");
                Type at_ = resolve_expr(e->args[0]);
                int key_ok = at_ == T_STRING || at_ == T_INT ||
                             (IS_NEWTYPE(at_) && (nt_under(at_) == T_INT || nt_under(at_) == T_STRING)) ||
                             enum_fieldless(at_) || (mapkey_composite(at_) && key_hashable(at_));
                if (!key_ok)
                    die_at(e->line, "hash(x) can't hash a %s (hashable = the map-key types: int, string, "
                                   "their newtypes, fieldless enums, and composites of those)", type_name(at_));
                if (mapkey_composite(at_) && g_nhashargs < 64) g_hashargs[g_nhashargs++] = at_;
                return e->type = T_INT;
            }
            if (!strcmp(e->sval, "to_float")) {   /* int/u32/u64/f32 -> float, or unwrap a float newtype */
                if (e->nargs != 1) die_at(e->line, "to_float(n) takes one argument");
                Type at_ = resolve_expr(e->args[0]);
                if (at_ != T_INT && !is_sized_int(at_) && at_ != T_F32 && !(IS_NEWTYPE(at_) && nt_under(at_) == T_FLOAT))
                    die_at(e->line, "to_float(n) takes an int, a sized int, f32, or a float newtype");
                return e->type = T_FLOAT;
            }
            if (!strcmp(e->sval, "to_int")) {   /* float/u32/u64/f32 -> int (truncate), or unwrap an int newtype */
                if (e->nargs != 1) die_at(e->line, "to_int(x) takes one argument");
                Type at_ = resolve_expr(e->args[0]);
                if (at_ == T_INT)   /* F3: the common `to_int(s[i])` confusion -- s[i] is already the byte value as int */
                    die_at(e->line, "to_int(x): x is already an int -- indexing a string (s[i]) yields the byte value as an int; use chr(x) for its one-character string");
                if (at_ != T_FLOAT && !is_sized_int(at_) && at_ != T_F32 && at_ != T_CHAR && !(IS_NEWTYPE(at_) && nt_under(at_) == T_INT))
                    die_at(e->line, "to_int(x) takes a float (truncates toward zero), a sized int, f32, a char, or an int newtype");
                return e->type = T_INT;
            }
            if (is_sized_conv(e->sval)) {   /* to_u8..to_i64, to_f32: any numeric scalar -> the named sized type */
                if (e->nargs != 1) die_at(e->line, "%s(x) takes one argument", e->sval);
                Type at_ = base_of(resolve_expr(e->args[0]));
                if (at_ != T_INT && at_ != T_CHAR && at_ != T_FLOAT && !is_sized_int(at_) && at_ != T_F32)
                    die_at(e->line, "%s(x) takes a numeric value", e->sval);
                return e->type = sized_conv_target(e->sval);
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
            if (!strcmp(e->sval, "to_bytes")) {   /* string -> bytes (same byte buffer); or [int] -> bytes (each elem & 0xFF: build a binary buffer a string can't hold) */
                if (e->nargs != 1) die_at(e->line, "to_bytes(s) takes one argument");
                Type at_ = base_of(resolve_expr(e->args[0]));
                if (at_ != T_STRING && at_ != T_BYTES && at_ != T_ARRAY_INT)
                    die_at(e->line, "to_bytes(x) takes a string or [int]");
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
                if (IS_BOUNDED(arrt))   /* v1: bounded supports push only; pop deferred */
                    die_at(e->line, "pop is not supported on a bounded[...] yet; read the last element via len()-1 and rebuild");
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
                if (!is_array(arrt) && !is_map(arrt))
                    die_at(e->line, "reserve's first argument must be an array or a map");
                if (IS_BOUNDED(arrt))   /* capacity is fixed at the type; reserve is meaningless */
                    die_at(e->line, "reserve does not apply to a bounded[...] — its capacity is fixed");
                /* reserve is a capacity hint: the scalar arrays have a runtime
                 * tycho_arr_{int,float,str}_reserve; composite-element arrays get a
                 * generated tycho_arr_C%d_reserve (emitted with the family). Maps get
                 * tycho_map_XX_reserve (runtime fast families) or a generated
                 * tycho_mapc%d_reserve (composite maps). SOA and
                 * other non-array element kinds have no reserve — fail closed. */
                if (!is_array(arrt) && !is_map(arrt))
                    die_at(e->line, "reserve only supports arrays of scalars, structs, tuples, or nested arrays, and maps");
                if (!is_lvalue(e->args[0]))
                    die_at(e->line, "cannot reserve through this expression");
                if (!vars_can_mutate(root->sval))
                    die_at(e->line, "cannot mutate parameter '%s' (it is borrowed read-only)", root->sval);
                if (resolve_exp(e->args[1], T_INT) != T_INT)
                    die_at(e->line, "reserve's capacity must be int");
                return e->type = T_VOID;
            }
            /* variadic call packing (2.2a): if the callee's last parameter is `...T`,
             * fold the trailing arguments into ONE array argument (or, for `x...`, use
             * that array directly) BEFORE generic inference / arity / type checks run.
             * After this, the call is an ordinary call to `f(fixed..., xs: [T])`. */
            if (!e->qual && !e->lhs) {
                int vnp = -1, vlast = 0, velem_generic = 0; Type velem = T_VOID;
                Proc *vgt = generic_find(e->sval);
                if (vgt) { vnp = vgt->nparams;
                    if (vnp > 0 && vgt->params[vnp - 1].is_variadic) { vlast = 1; velem = arr_elem(vgt->params[vnp - 1].type); velem_generic = IS_TYPARAM(velem); } }
                else { Sig *vs = sig_find(e->sval);
                    if (vs && !vs->builtin) { vnp = vs->nparams;
                        if (vnp > 0 && vs->variadic[vnp - 1]) { vlast = 1; velem = arr_elem(vs->params[vnp - 1]); } } }
                if (vlast) {
                    int nfixed = vnp - 1;
                    if (e->nargs < nfixed)
                        die_at(e->line, "'%s' takes at least %d argument(s), got %d", e->sval, nfixed, e->nargs);
                    int ntrail = e->nargs - nfixed, spread = 0;
                    for (int i = nfixed; i < e->nargs; i++) if (e->args[i]->kind == E_SPREAD) spread = 1;
                    Expr *varg;
                    if (spread) {
                        if (ntrail != 1 || e->args[nfixed]->kind != E_SPREAD)
                            die_at(e->line, "a spread argument `x...` must be the only variadic argument to '%s'", e->sval);
                        varg = e->args[nfixed]->lhs;   /* unwrap; its [T] type is checked in the arg loop below */
                    } else {
                        Expr *lit = new_expr(E_ARRLIT, e->line);
                        lit->nargs = ntrail;
                        if (ntrail > 0) {
                            lit->args = (Expr **)xmalloc((size_t)ntrail * sizeof(Expr *));
                            for (int i = 0; i < ntrail; i++) lit->args[i] = e->args[nfixed + i];
                        } else if (velem_generic) {
                            die_at(e->line, "cannot infer the element type of an empty variadic call to generic '%s'; pass at least one argument", e->sval);
                        } else { lit->type = arr_of(velem); lit->ival = (int)arr_of(velem); }   /* typed empty [T] */
                        varg = lit;
                    }
                    Expr **na = (Expr **)xmalloc((size_t)(nfixed + 1) * sizeof(Expr *));
                    for (int i = 0; i < nfixed; i++) na[i] = e->args[i];
                    na[nfixed] = varg;
                    e->args = na; e->nargs = nfixed + 1;
                }
            }
            { Proc *gt = generic_find(e->sval);   /* generics: infer type args, intern instance, rewrite e->sval */
              if (gt && !e->qual && !e->lhs) instantiate_generic(gt, e);
              else if (e->ntypeargs > 0) die_at(e->line, "explicit type arguments given, but '%s' is not a generic function", e->sval); }
            Sig *s = sig_find(e->sval);
            if (!s) {
                int cl_strong = 0;
                const char *cl = corelib_hint(e->sval, &cl_strong);   /* F8: a corelib package/function by this name? */
                if (cl && cl_strong) die_at(e->line, "unknown procedure '%s' -- %s", e->sval, cl);   /* high-confidence stdlib match beats a weak local typo */
                const char *sg = suggest_fn(e->sval);                 /* a user fn / builtin typo */
                if (sg) die_at(e->line, "unknown procedure '%s'; did you mean '%s'?", e->sval, sg);
                if (cl) die_at(e->line, "unknown procedure '%s' -- %s", e->sval, cl);   /* weak stdlib guess, but nothing local matched */
                die_at(e->line, "unknown procedure '%s'", e->sval);
            }
            if (e->nargs != s->nparams)
                die_at(e->line, "'%s' takes %d argument(s), got %d",
                       e->sval, s->nparams, e->nargs);
            for (int i = 0; i < e->nargs; i++) {
                g_in_arg++;
                Type at_ = resolve_exp(e->args[i], s->params[i]);   /* fixes a None arg */
                g_in_arg--;
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
                if (at_ != s->params[i]) {
                    /* F6: a print-family (or other builtin) string param handed a
                     * str()-able scalar -- point at the fix instead of just the type. */
                    Type ab = base_of(at_);
                    int strable = (ab == T_INT || ab == T_BOOL || ab == T_FLOAT || ab == T_CHAR ||
                                   is_sized_int(ab) || ab == T_F32 ||
                                   /* F5: aggregates are str()-able too */
                                   is_array(ab) || is_map(ab) || IS_STRUCT(ab) || IS_ENUM(ab) ||
                                   IS_TUP(ab) || IS_OPT(ab) || IS_RES(ab));
                    if (s->builtin && s->params[i] == T_STRING && strable)
                        die_at(e->line, "argument %d of '%s' is %s, expected string -- wrap it with str(...), e.g. %s(str(x))",
                               i + 1, e->sval, type_name(at_), e->sval);
                    die_at(e->line, "argument %d of '%s' is %s, expected %s",
                           i + 1, e->sval, type_name(at_), type_name(s->params[i]));
                }
            }
            /* exclusivity (Law of Exclusivity): the same variable may not be
             * passed to two inout params of one call — that would be two
             * overlapping writes, breaking the x = f(x) value-semantics model.
             * No globals/closures exist in Tycho, so this is the only alias. */
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
            /* Also forbid passing a WHOLE variable inout AND by value in the same
             * call: value semantics makes the by-value arg an independent copy, but
             * it would alias the value the inout mutates (the copy is not taken
             * before the mutation). Swift forbids this overlapping access; reject it.
             * Scoped to a BARE by-value variable (f(&a, a)) -- a sub-element like
             * a[i]/a.f is a distinct extracted value and is left alone. */
            for (int i = 0; i < e->nargs; i++) {
                if (!s->inout[i]) continue;
                Expr *ri = e->args[i]->lhs;
                while (ri->kind == E_FIELD || ri->kind == E_INDEX) ri = ri->lhs;
                if (ri->kind != E_IDENT) continue;
                for (int j = 0; j < e->nargs; j++) {
                    if (j == i || s->inout[j]) continue;       /* only a by-value arg */
                    Expr *rj = e->args[j];                     /* the WHOLE variable, not a sub-place */
                    if (rj->kind == E_IDENT && !strcmp(rj->sval, ri->sval))
                        die_at(e->line, "variable '%s' is passed to an inout parameter and also by value in the same call to '%s' (overlapping access — the by-value copy would alias the inout'd value)", ri->sval, e->sval);
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
            if (e->op == TK_TILDE) {   /* unary bitwise NOT: operand in lhs, rhs NULL */
                Type ot = resolve_expr(e->lhs);
                if (ot != T_INT && !is_sized_int(ot))
                    die_at(e->line, "unary `~` needs an integer");
                return e->type = ot;   /* ~u32 is a u32; ~u8 is a u8 (truncated to its width) */
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
            if (e->lhs->kind == E_INT && is_sized_int(rt)) { e->lhs->type = rt; lt = rt; }
            if (e->rhs->kind == E_INT && is_sized_int(lt)) { e->rhs->type = lt; rt = lt; }
            if (rt == T_F32 && (e->lhs->kind == E_INT || e->lhs->kind == E_FLOAT)) {
                f32_lit(e->lhs); lt = T_F32;
            }
            if (lt == T_F32 && (e->rhs->kind == E_INT || e->rhs->kind == E_FLOAT)) {
                f32_lit(e->rhs); rt = T_F32;
            }
            if (is_cmp(e->op)) {
                if (e->op == TK_EQEQ || e->op == TK_NEQ) {
                    /* equality is structural for every type (value semantics):
                     * ints/bools directly, strings/arrays/structs by content,
                     * recursing through nesting. Only void is incomparable. */
                    if (lt != rt)
                        die_at(e->line, "cannot compare %s with %s", type_name(lt), type_name(rt));
                    if (lt == T_VOID) die_at(e->line, "cannot compare void");
                    /* functions are the one type with no structural equality: two
                     * closures with different captured envs but identical behavior
                     * are indistinguishable, and comparing thunk pointers is a
                     * non-structural leak. Reject it (Go/Swift/Odin all do). */
                    if (IS_FUNC(lt))
                        die_at(e->line, "functions are not comparable -- closures have no structural equality; "
                               "compare the values they produce instead");
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
                                          b == T_F32 || is_sized_int(b));
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
            /* bytes + bytes / bytes + char: the same two concats, because bytes IS
             * string's buffer (:498). No implicit widening from string: crossing
             * the intent boundary stays explicit (to_bytes/to_str). */
            if (e->op == TK_PLUS && lt == T_BYTES) {
                if (rt != T_BYTES && rt != T_CHAR)
                    die_at(e->line, "cannot concatenate bytes with %s -- to_bytes(x) to widen, or to_str(b) to work in strings", type_name(rt));
                return e->type = T_BYTES;
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
                int lok = (lt == T_INT || is_sized_int(lt));
                int rok = (rt == T_INT || is_sized_int(rt));
                if (!lok || !rok)
                    die_at(e->line, "shift operators require integer operands (got %s, %s)",
                           type_name(lt), type_name(rt));
                return e->type = lt;   /* result width = the shifted value's */
            }
            /* Element-wise arithmetic on arrays (post-freeze). `a OP b` yields a
             * FRESH array whose i-th element is `a[i] OP b[i]`; value semantics
             * means neither operand is aliased or mutated by it. Legality is
             * exactly the scalar rule for the element type — see elem_arith_ok,
             * which derives the per-type operator set from the scalar arms below.
             *
             * Placed BEFORE the modulo/bitwise arm on purpose: `%` on two int
             * arrays has to reach this arm rather than die there. `&`, `|`, `^`
             * and the two shifts are not arithmetic and are not listed here, so
             * they fall through to those arms and refuse an array exactly as
             * they do today.
             *
             * One array and one scalar is handled by the broadcast arm directly
             * below, which shares this arm's legality rule. */
            if (is_array(lt) && is_array(rt) &&
                (e->op == TK_PLUS || e->op == TK_MINUS || e->op == TK_STAR ||
                 e->op == TK_SLASH || e->op == TK_PERCENT)) {
                /* bounded[N]T carries a capacity and a separate live length, and
                 * [$N]T is a template-only size parameter. Neither has a settled
                 * element-wise meaning, so refuse rather than guess. */
                if (IS_BOUNDED(lt) || IS_BOUNDED(rt) || IS_SIZEPARAM_ARR(lt) || IS_SIZEPARAM_ARR(rt))
                    die_at(e->line, "element-wise `%s` is defined for [T] and [N]T only (got %s, %s)",
                           arith_op_spell(e->op), type_name(lt), type_name(rt));
                if (arr_elem(lt) != arr_elem(rt))
                    die_at(e->line, "element-wise `%s` requires two arrays with the same element type (got %s, %s)",
                           arith_op_spell(e->op), type_name(lt), type_name(rt));
                /* a fixed [N]T and a growable [T] cannot agree on a length at
                 * compile time and the mismatch rule differs between them (one
                 * is a compile error, the other a runtime abort). Fail closed. */
                if (IS_FIXARR(lt) != IS_FIXARR(rt))
                    die_at(e->line, "cannot mix a fixed array and a growable array in element-wise `%s` (got %s, %s) -- "
                           "copy one side into the other's kind first",
                           arith_op_spell(e->op), type_name(lt), type_name(rt));
                /* same element type, same kind, still different types => two
                 * fixed arrays with different static lengths. `==` already
                 * requires the same static N to compare two fixed arrays
                 * element-wise; arithmetic requires it for the same reason. */
                if (lt != rt)
                    die_at(e->line, "element-wise `%s` on a fixed array requires the same static length (got %s and %s)",
                           arith_op_spell(e->op), type_name(lt), type_name(rt));
                if (!elem_arith_ok(e->op, arr_elem(lt)))
                    die_at(e->line, "`%s` is not defined element-wise on %s, because `%s` is not defined on %s",
                           arith_op_spell(e->op), type_name(lt), arith_op_spell(e->op), type_name(arr_elem(lt)));
                return e->type = lt;
            }
            /* Scalar broadcast (post-freeze). `a OP s` and `s OP a` scale every
             * element: a FRESH array whose i-th element is `a[i] OP s` (resp.
             * `s OP a[i]`). The scalar may sit on either side, and for the
             * non-commutative operators the ORDER IS KEPT -- `2 - [5,1]` is
             * `[-3,1]`, never `[3,-1]`. Nothing here swaps the operands, and
             * gen_ew_arith keeps lhs on the left too.
             *
             * Placed with the array OP array arm, i.e. after the shift arm and
             * before the modulo/bitwise arm, for the same reason: `%` against an
             * array must reach here, `&`/`|`/`^`/shifts must not.
             *
             * Literal adaptation does NOT fall out of the arms below. Every one
             * of them keys off the OTHER operand's scalar type -- the sized-int
             * rule tests is_sized_int(rt), the f32 rule tests T_F32, the B-1
             * float rule tests T_FLOAT -- and an array type is none of those, so
             * none of them fires for `[1.0, 2.0] * 2`. The three rules are
             * therefore re-applied here against the ELEMENT type, with the same
             * value-directional restriction: a LITERAL adapts, a variable never
             * widens. `[1.0,2.0] * 2` is then exactly `1.0 * 2`, and
             * `[1.0,2.0] * n` for an int variable `n` is refused exactly as
             * `1.0 * n` is.
             *
             * After adaptation the scalar must be AT the element type. That is
             * the rule the array OP array arm above applies (two arrays must
             * share an element type), so `['a','b'] + 1` is refused here just as
             * `[char] + [int]` is refused there -- even though the scalar
             * `'a' + 1` is legal further down. Fail closed, and consistent with
             * the arm this one extends. */
            if (is_array(lt) != is_array(rt) &&
                (e->op == TK_PLUS || e->op == TK_MINUS || e->op == TK_STAR ||
                 e->op == TK_SLASH || e->op == TK_PERCENT)) {
                int lhs_is_arr = is_array(lt);
                Type at = lhs_is_arr ? lt : rt;
                Type st = lhs_is_arr ? rt : lt;
                Expr *sc = lhs_is_arr ? e->rhs : e->lhs;
                if (IS_BOUNDED(at) || IS_SIZEPARAM_ARR(at))
                    die_at(e->line, "element-wise `%s` is defined for [T] and [N]T only (got %s, %s)",
                           arith_op_spell(e->op), type_name(lt), type_name(rt));
                Type et = arr_elem(at);
                if (sc->kind == E_INT && is_sized_int(et)) { sc->type = et; st = et; }
                else if (et == T_F32 && (sc->kind == E_INT || sc->kind == E_FLOAT)) {
                    f32_lit(sc); st = T_F32;
                } else if (et == T_FLOAT && st == T_INT && sc->kind == E_INT) {
                    sc->kind = E_FLOAT; sc->fval = (double)sc->ival; sc->type = T_FLOAT; st = T_FLOAT;
                }
                if (st != et)
                    die_at(e->line, "element-wise `%s` requires the scalar to have the array's element type (got %s and %s) -- "
                           "a literal adapts, but a variable never widens",
                           arith_op_spell(e->op), type_name(lt), type_name(rt));
                if (!elem_arith_ok(e->op, et))
                    die_at(e->line, "`%s` is not defined element-wise on %s, because `%s` is not defined on %s",
                           arith_op_spell(e->op), type_name(at), arith_op_spell(e->op), type_name(et));
                return e->type = at;
            }
            /* modulo / bitwise: integer-only, both operands the same integer type
             * (int, u32, or u64) — no implicit int/u32 mixing beyond literal adaptation. */
            if (e->op == TK_PERCENT || e->op == TK_AMP || e->op == TK_PIPE || e->op == TK_CARET) {
                if ((lt != T_INT && !is_sized_int(lt)) || lt != rt)
                    die_at(e->line, "modulo / bitwise operators require two matching integers (got %s, %s)",
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
            if (lt == rt && is_sized_int(lt)) return e->type = lt;   /* u8/u16/u32/u64/i8/i16/i32/i64 wrap at their width */
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
            /* `to_float(x)` / `to_int(x)` is useless advice for a buffer, and it was
             * what `bytes + bytes` used to be told (FRICTION.md:225). Name the three
             * operators bytes actually has instead. */
            if (lt == T_BYTES || rt == T_BYTES)
                die_at(e->line, "bytes has no arithmetic (got %s, %s) -- bytes supports `a + b` and `b + 'c'` (concat), `b[i]` (the byte value, an int) and `b[i:j]` (a sub-buffer); for anything else use to_str(b)",
                       type_name(lt), type_name(rt));
            die_at(e->line, "arithmetic requires two ints or two floats (got %s, %s) -- convert one side, e.g. to_float(x) to compute in floats, or to_int(x) in ints",
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
    /* [e0, ..., eN-1] checked against an ARRAY destination: fixed `[N]T` (1.6, count must match) or dynamic `[T]` (any count). Each
     * element is checked AGAINST T, so literal adaptation reaches the elements -- `a: [u32] = [1, 2]`, which used to die "declared type
     * [u32] but value is [int]" while the fixed form compiled (the loops-cleanup plan). Bounded is the branch below; a `[$N]T`/`[$T]` template destination stays out (those resolve after substitution); with no destination a bracket literal still synthesizes a dynamic `[T]`. */
    if (e->kind == E_ARRLIT && e->nargs > 0 && e->op != TK_COLON && is_array(want) && !IS_BOUNDED(want) && !IS_SIZEPARAM_ARR(want) && !has_typaram(want)) {
        int64_t n = IS_FIXARR(want) ? fixarr_size(want) : e->nargs;   /* a dynamic [T] accepts any count */
        if (e->nargs != n)
            die_at(e->line, "a fixed-size array of length %lld needs %lld elements, got %d", (long long)n, (long long)n, e->nargs);
        Type el = arr_elem(want);
        for (int i = 0; i < e->nargs; i++)
            if (resolve_exp(e->args[i], el) != el)
                die_at(e->line, "element %d of a %s array is the wrong type", i + 1, type_name(want));
        return e->type = want;
    }
    /* [e0, ..., ek-1] coerces to a bounded[N]T when the destination is bounded and the
     * count fits the capacity (k <= N). len becomes k; the rest of the inline v[] is unused. */
    if (e->kind == E_ARRLIT && e->nargs > 0 && e->op != TK_COLON && IS_BOUNDED(want)) {
        int64_t cap = bounded_cap(want);
        if (e->nargs > cap)
            die_at(e->line, "a bounded[%lld] holds at most %lld elements, got %d", (long long)cap, (long long)cap, e->nargs);
        Type el = arr_elem(want);
        for (int i = 0; i < e->nargs; i++)
            if (resolve_exp(e->args[i], el) != el)
                die_at(e->line, "element %d of a %s literal is the wrong type", i + 1, type_name(want));
        return e->type = want;
    }
    if (e->kind == E_INT && want == T_FLOAT) {   /* int literal adapts to a float context (B-1; literals only) */
        e->kind = E_FLOAT;
        e->fval = (double)e->ival;
        return e->type = T_FLOAT;
    }
    /* sized-numeric literal adapts to a u32/u64/f32 context (a decl annotation,
     * return type, or arg) — `w: u32 = 5`, `return 0u64`, `f(3.0)` where f wants f32. */
    if (e->kind == E_INT && is_sized_int(want)) return e->type = want;
    if ((e->kind == E_INT || e->kind == E_FLOAT) && want == T_F32) {
        f32_lit(e);
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
    /* AUDIT: Some(x)/Ok(x)/Err(x) checked against a matching Option/Result — push the
     * expected inner type into the payload so a bare None/Ok/Err at ANY nesting depth
     * is fixed from context (Some(None) : Option(Option(int)), Ok(None), Some(Ok(1))).
     * Without this the synthesis-only E_SOME (:4371) dies on a bare payload. Swift. */
    if (e->kind == E_SOME && IS_OPT(want)) {
        Type in = resolve_exp(e->lhs, opt_inner(want));
        if (in != opt_inner(want))
            die_at(e->line, "declared type %s but value is Some(%s)", type_name(want), type_name(in));
        return e->type = want;
    }
    if ((e->kind == E_OK || e->kind == E_ERR) && IS_RES(want)) {
        Type half = e->kind == E_OK ? res_ok(want) : res_err(want);
        Type in = resolve_exp(e->lhs, half);
        if (in != half)
            die_at(e->line, "declared type %s but value is %s(%s)", type_name(want),
                   e->kind == E_OK ? "Ok" : "Err", type_name(in));
        return e->type = want;
    }
    /* (e1, ..., en) checked against a tuple destination of the same arity: push
     * each element's expected type INTO the element, so a `Result`/`Option`/bare
     * `[]` element grounds from context exactly as it does as a bare return or a
     * struct field (FRICTION.md:159 -- `return (Err(A), "partial")` from a
     * `-> (Result(int, E), string)` fn used to die "tuple element 1 needs a
     * concrete value", because E_TUPLE was synthesis-only and Ok/Err synthesize
     * to T_OK_PARTIAL/T_ERR_PARTIAL). Each element is resolved EXACTLY ONCE and
     * the SYNTHESIZED element types are returned, not `want`: a mismatch then
     * reports through the caller's own equality check with its own message
     * instead of a second visit to the same node (the friction plan). */
    if (e->kind == E_TUPLE && IS_TUP(want) && e->nargs == tup_n(want) &&
        e->nargs >= 2 && e->nargs <= 8) {
        Type elems[8];
        for (int i = 0; i < e->nargs; i++) {
            Type et = resolve_exp(e->args[i], tup_elem(want, i));
            if (et == T_VOID || et == T_NONE || et == T_OK_PARTIAL || et == T_ERR_PARTIAL)
                die_at(e->line, "tuple element %d needs a concrete value", i + 1);
            elems[i] = et;
        }
        return e->type = tup_of(elems, e->nargs);
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
            die_at(e->line, "parallel for cannot pass a captured variable as inout (no shared mutation across chunks)");
    }
    /* An in-place mutating builtin applied to a CAPTURED collection is the same
     * soundness violation S_INDEXSET/S_FIELDSET catch below (src/tychoc.c:6549),
     * and it must get the same message. `push`/`pop` are the pair the tree
     * already treats as mutating their first argument -- the while-loop mutation
     * scan uses exactly this test (src/tychoc.c:6830). Before this, `push(xs, i)`
     * inside a `parallel for` over a captured `xs` fell through the parfor scan
     * and was refused DOWNSTREAM by the generic borrow rule, on the lifted chunk
     * proc's parameter: `cannot mutate parameter 'xs' (it is borrowed
     * read-only...)`, telling the user to copy an array they never wrote as a
     * parameter, and leaving this gate's coverage resting on a rule in another
     * pass. Fail-closed either way -- the program was already rejected -- so this
     * changes the diagnostic, not the accept/reject verdict. the loops-cleanup plan. */
    if (e->kind == E_CALL && e->sval && e->nargs >= 1 &&
        (!strcmp(e->sval, "push") || !strcmp(e->sval, "pop"))) {
        Expr *root = e->args[0];
        while (root && (root->kind == E_FIELD || root->kind == E_INDEX || root->kind == E_TUPIDX))
            root = root->lhs;
        if (root && root->kind == E_IDENT && !pf_local(root->sval)) {
            Type vt;
            if (vars_find(root->sval, &vt))
                die_at(e->line, "parallel for cannot mutate captured variable '%s' in place", root->sval);
        }
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
            pf_scan_expr(s->r_start); pf_scan_expr(s->r_stop);
            int save = g_pf_nloc;
            pf_add_local(s->name);
            pf_scan_body(s->body, s->nbody, loopdepth + 1);
            g_pf_nloc = save;
            return;
        }
        case S_FOR3: {   /* a three-clause loop nested in a parallel-for body */
            int save = g_pf_nloc;
            pf_scan_stmt(s->els[0], loopdepth);   /* init: an S_DECL registers its name as chunk-local */
            pf_scan_expr(s->expr);
            pf_scan_body(s->body, s->nbody, loopdepth + 1);   /* body + post */
            g_pf_nloc = save;
            return;
        }
        case S_MATCH: {
            pf_scan_expr(s->expr);
            for (int a = 0; a < s->narms; a++) {
                int save = g_pf_nloc;
                for (int b = 0; b < s->arms[a].nbinds; b++) pf_add_local(s->arms[a].binds[b]);
                for (int b = 0; b < s->arms[a].nsubbinds; b++) pf_add_local(s->arms[a].subbinds[b]);
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
             * cannot cross, no inout-capture, reductions only). */
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
            s->name = "__pw"; s->r_start = z; s->r_stop = nc;
            Stmt *sel = new_stmt(S_SELECT, s->line);
            sel->arms = (MatchArm *)xmalloc(2 * sizeof(MatchArm));
            sel->sel_ch = (Expr **)xmalloc(2 * sizeof(Expr *));
            memset(sel->arms, 0, 2 * sizeof(MatchArm));   /* sub = NULL: no nested pattern */
            sel->arms[0].sub_vi = sel->arms[1].sub_vi = -1;
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
            s->name = iv; s->r_start = z; s->r_stop = lc;
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
    /* HISTORY: a fail-closed `if (s->r_step) die_at(s->line, "parallel for does not
     * support a range step")` stood here until 2026-07-30, unreachable since `range()`
     * went on 2026-07-29. It was deleted with the field -- the loops-cleanup plan. */
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
     * spawn site gen_exprs them there, with inout deref handled as usual) */
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
    /* Every field spelled out (name, type, is_inout, is_sink, is_variadic,
     * ffi_ct) so a future Param field re-raises -Wmissing-field-initializers
     * here and forces a decision, instead of silently defaulting.
     * is_sink MUST be 0, and not merely by accident: `sink` means an OWNED
     * value the callee may consume once (is_sink_param -> can_move_from,
     * :7271/:7858). Every chunk proc is handed the SAME capture values, and the
     * bounds/captures are borrows of the enclosing scope, so consuming one in
     * any chunk would hand off a buffer another chunk still reads. 0 is the
     * required value, and it matches the lambda-lift twin at :4533 which sets
     * `caps[ncap].is_sink = 0` explicitly. is_variadic is 0 (a synthesized
     * chunk proc has a fixed arity) and ffi_ct is NULL (no FFI boundary). */
    pr->params[0] = (Param){ "__plo", T_INT, 0, 0, 0, NULL };
    pr->params[1] = (Param){ "__phi", T_INT, 0, 0, 0, NULL };
    for (int i = 0; i < pf->ncap; i++)
        pr->params[2 + i] = (Param){ pf->caps[i]->sval, pf->caps[i]->type, 0, 0, 0, NULL };
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
    loop->r_start = lo; loop->r_stop = hi;
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
 * to it, a place-mutation of it (v[i]=, v.f=), or being passed by `&` (inout) --
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

/* mutations/exits hiding in an expression: &v (inout), push/pop(v,...), die(...) */
static void wl_scan_expr(Expr *e, const char *muts[], int *nm, int *exit) {
    if (!e) return;
    if (e->kind == E_ADDR) wl_add(muts, nm, wl_root(e->lhs));
    if (e->kind == E_CALL && e->sval) {
        if (!strcmp(e->sval, "die")) *exit = 1;
        if ((!strcmp(e->sval, "push") || !strcmp(e->sval, "pop")) && e->nargs >= 1)
            wl_add(muts, nm, wl_root(e->args[0]));
    }
    wl_scan_expr(e->lhs, muts, nm, exit);
    wl_scan_expr(e->rhs, muts, nm, exit);
    for (int i = 0; i < e->nargs; i++) wl_scan_expr(e->args[i], muts, nm, exit);
}

static void wl_scan_body(Stmt **body, int n, const char *muts[], int *nm, int *exit) {
    for (int i = 0; i < n; i++) {
        Stmt *s = body[i];
        if (!s) continue;
        switch (s->kind) {
            case S_ASSIGN: case S_DECL: case S_FORRANGE: wl_add(muts, nm, s->name); break;
            case S_MDECL: case S_MASSIGN:
                for (int j = 0; j < s->nnames; j++) wl_add(muts, nm, s->names[j]);
                break;
            case S_INDEXSET: case S_FIELDSET: wl_add(muts, nm, wl_root(s->target)); break;
            case S_RETURN: case S_BREAK: *exit = 1; break;
            default: break;
        }
        wl_scan_expr(s->expr, muts, nm, exit);
        wl_scan_expr(s->target, muts, nm, exit);
        wl_scan_expr(s->r_start, muts, nm, exit);
        wl_scan_expr(s->r_stop, muts, nm, exit);
        /* no r_step to scan: every S_FORRANGE steps by 1 (the loops-cleanup plan). */
        wl_scan_body(s->body, s->nbody, muts, nm, exit);
        wl_scan_body(s->els, s->nels, muts, nm, exit);
        for (int a = 0; a < s->narms; a++) wl_scan_body(s->arms[a].body, s->arms[a].nbody, muts, nm, exit);
        if (s->ctrl) wl_scan_body(&s->ctrl, 1, muts, nm, exit);   /* value if/match decl */
    }
}

static void wl_check(Stmt *s) {           /* s is an S_WHILE */
    const char *cv[WL_MAX]; int ncv = 0, has_call = 0;
    wl_cond_vars(s->expr, cv, &ncv, &has_call);
    if (ncv == 0 || has_call) return;     /* constant cond (`for true:`) or a call in it: skip */
    const char *muts[WL_MAX]; int nm = 0, exit = 0;
    wl_scan_body(s->body, s->nbody, muts, &nm, &exit);
    if (exit) return;                     /* a break/return/die can end the loop */
    if (nm >= WL_MAX) return;             /* muts table overflowed -- a mutation may have been dropped, so don't risk a false warning */
    for (int i = 0; i < ncv; i++)
        for (int j = 0; j < nm; j++)
            if (!strcmp(cv[i], muts[j])) return;   /* a condition variable is changed -> progresses */
    warn_at(s->line, "loop condition never changes; this `for` may run forever "
                     "(forgot to advance a variable? consider `for i := 0; i < n; i += 1:`)");
}

/* "pure" builtins have no side effect, so discarding the result in statement
 * position is always a no-op -- e.g. map_set/map_del return a NEW map (a common
 * footgun: `map_set(m,k,v)` as a bare statement does nothing). */
static int is_pure_builtin(const char *n) {
    if (!n) return 0;
    static const char *pure[] = { "str", "substr", "chr", "split", "keys", "find", "char_at",
        "map_get", "map_has", "map_set", "map_del", "sqrt", "pow", "floor", "fabs",
        "to_float", "to_int", "to_char", "to_str", "to_bool", "is_null", "len", "hash", 0 };
    for (int i = 0; pure[i]; i++) if (!strcmp(n, pure[i])) return 1;
    return 0;
}

/* A discarded `m.get(k[, d])` statement is a pure map read whose value is thrown
 * away -- warn like any pure builtin. Detected on the PRE-resolve method-call
 * form (receiver `m` is a variable that resolves to a map), which distinguishes it
 * from a bare `m[k]` statement (index syntax, deliberately left un-warned). Returns
 * the display name to warn about, or NULL. */
static const char *discarded_map_get(Expr *e) {
    if (!e || e->kind != E_CALL || !e->sval || strcmp(e->sval, "get") || e->nargs < 1 || !e->qual) return NULL;
    Type rt;
    if (vars_find(e->qual, &rt) && is_map(rt)) return "m.get";
    return NULL;
}

/* ---- nested match patterns (the friction plan / FRICTION.md:139) --------------
 *
 * One `match` side (the Ok, Err or Some arms) is a small ordered decision list:
 * zero or more REFINED arms, each testing one variant of the payload's enum, and
 * at most one UNREFINED arm binding the payload whole. `plain` records the
 * unrefined arm's line (0 = none) so a refined arm written AFTER it is rejected as
 * dead rather than silently unreachable; `cov` marks the variants the refined arms
 * matched, which is what makes the side exhaustive without an unrefined arm. */
typedef struct { int plain; int *cov; int ncov; } SideCov;

/* Is this side of the match total? Either its unrefined arm covers it, or the
 * refined arms named every variant of the payload enum. */
static int side_total(SideCov *sc, Type pt) {
    if (sc->plain) return 1;
    if (!sc->cov) return 0;
    for (int v = 0; v < sc->ncov; v++) if (!sc->cov[v]) return 0;
    (void)pt;
    return 1;
}

/* Check one Ok/Err/Some arm against its payload type `pt`, push what it binds, and
 * record it in `sc`. Sets arm->sub_vi to the refined variant index, or -1.
 *
 * This is where a bare `Err(A)` is PROMOTED from a binding to a pattern. Doing it
 * here rather than in the parser is the whole point: only the resolver knows that
 * the payload is an enum with a variant `A`. It is idempotent -- a promoted arm has
 * arm->sub set and skips the promotion branch -- because a generic function's body
 * is cloned per instance and re-resolved (the friction plan: resolution is not
 * single-pass, and a non-idempotent in-place rewrite is exactly what bit there). */
static void match_arm_payload(MatchArm *arm, Type pt, const char *tag, SideCov *sc) {
    if (sc->plain)
        die_at(arm->line, "duplicate %s arm", tag);   /* an unrefined arm already took this side */
    if (!arm->sub) {
        if (arm->nbinds != 1)
            die_at(arm->line, "%s(x) binds exactly one value", tag);
        int vi = enum_variant_index(pt, arm->binds[0]);
        if (vi < 0) {                                 /* an ordinary binding */
            vars_push(arm->binds[0], pt, 1);
            arm->sub_vi = -1;
            sc->plain = arm->line ? arm->line : 1;
            return;
        }
        if (g_enums[ENUM_ID(pt)].variants[vi].npayload != 0)
            die_at(arm->line, "'%s' is a variant of %s that carries a payload -- write "
                   "%s(%s(x)) to match it, or rename the binding",
                   arm->binds[0], type_name(pt), tag, arm->binds[0]);
        arm->sub = arm->binds[0]; arm->nbinds = 0; arm->nsubbinds = 0;
    }
    if (!IS_ENUM(pt))
        die_at(arm->sub_line, "a nested pattern needs an enum payload, but %s here carries %s",
               tag, type_name(pt));
    int vi = enum_variant_index(pt, arm->sub);
    if (vi < 0)
        die_at(arm->sub_line, "'%s' is not a variant of %s", arm->sub, type_name(pt));
    EnumDef *ed = &g_enums[ENUM_ID(pt)];
    Variant *var = &ed->variants[vi];
    if (arm->nsubbinds != var->npayload)
        die_at(arm->sub_line, "%s binds %d value(s), got %d", var->raw, var->npayload, arm->nsubbinds);
    for (int b = 0; b < arm->nsubbinds; b++) vars_push(arm->subbinds[b], var->payload[b], 1);
    arm->sub_vi = vi;
    if (!sc->cov) { sc->ncov = ed->nvariants; sc->cov = (int *)calloc((size_t)sc->ncov, sizeof(int)); }
    if (sc->cov[vi]) die_at(arm->sub_line, "duplicate %s(%s) arm", tag, var->raw);
    sc->cov[vi] = 1;
}

/* >0 while resolving the single-expr tails of a value if/match (see S_EXPR case) */
static int g_value_ctrl = 0;
static void resolve_stmt(Stmt *s, Type ret) {
    /* A bare subscript call as an assignment target — `r.at(i) = v` — parses as a
     * place-set with an E_CALL target. Resolve it in place context so the subscript
     * rewrites to its yielded place, then correct the statement kind to the resolved
     * place (E_INDEX -> index-set, else field/tuple-set). A plain call is not a place. */
    if ((s->kind == S_INDEXSET || s->kind == S_FIELDSET) && s->target && s->target->kind == E_CALL) {
        g_place = 1;
        resolve_expr(s->target);
        g_place = 0;
        if (s->target->kind == E_CALL)
            die_at(s->line, "cannot assign to this expression");
        s->kind = (s->target->kind == E_INDEX) ? S_INDEXSET : S_FIELDSET;
    }
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
                /* Every branch diverged (die/exit only), so nothing can be bound and
                 * `t` would stay at its T_VOID sentinel and be pushed as the var's
                 * type. Fail closed with the fix rather than declare a void local. */
                if (nt == 0)
                    die_at(s->line, "every branch of this value if/match diverges, so there is no value to bind to '%s' -- write the if/match as a plain statement", s->name);
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
                if (s->arms[i].variant && !strcmp(s->arms[i].variant, "_")) {
                    if (i != s->narms - 1) die_at(s->arms[i].line, "a `_` wildcard must be the last match arm");
                    if (s->arms[i].nbinds != 0) die_at(s->arms[i].line, "a `_` wildcard binds nothing");
                    wild = 1;
                }
            if (IS_OPT(st)) {
                Type inner = opt_inner(st);
                SideCov some = {0}; int none = 0;
                for (int i = 0; i < s->narms; i++) {
                    MatchArm *arm = &s->arms[i];
                    int m = vars_mark();
                    if (!strcmp(arm->variant, "_")) {
                        /* catch-all: no binding */
                    } else if (!strcmp(arm->variant, "Some")) {
                        match_arm_payload(arm, inner, "Some", &some);
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
                if (!wild && (!side_total(&some, inner) || !none))
                    die_at(s->line, "match on an Option must cover both Some and None");
                free(some.cov);
            } else if (IS_RES(st)) {
                Type okt = res_ok(st), errt = res_err(st);
                SideCov ok = {0}, err = {0};
                for (int i = 0; i < s->narms; i++) {
                    MatchArm *arm = &s->arms[i];
                    int m = vars_mark();
                    if (!strcmp(arm->variant, "_")) {
                        /* catch-all: no binding */
                    } else if (!strcmp(arm->variant, "Ok")) {
                        match_arm_payload(arm, okt, "Ok", &ok);
                    } else if (!strcmp(arm->variant, "Err")) {
                        match_arm_payload(arm, errt, "Err", &err);
                    } else {
                        die_at(arm->line, "a Result's arms are Ok(x), Err(e), and _, not '%s'", arm->variant);
                    }
                    resolve_block(arm->body, arm->nbody, ret);
                    vars_restore(m);
                }
                if (!wild && (!side_total(&ok, okt) || !side_total(&err, errt)))
                    die_at(s->line, "match on a Result must cover both Ok and Err");
                free(ok.cov); free(err.cov);
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
                    /* A nested pattern is supported on an Option/Result payload only
                     * (match_arm_payload). Reject the spellings that would otherwise
                     * bind silently here -- the same trap one level down. */
                    if (arm->sub)
                        die_at(arm->sub_line, "a nested pattern is only supported inside "
                               "Ok(...), Err(...) or Some(...) -- match %s's payload in its own match",
                               var->raw);
                    for (int b = 0; b < arm->nbinds; b++)
                        if (enum_variant_index(var->payload[b], arm->binds[b]) >= 0)
                            die_at(arm->line, "'%s' is a variant of %s, not a binding name -- "
                                   "match it in its own match, or rename the binding",
                                   arm->binds[b], type_name(var->payload[b]));
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
            } else if (st == T_INT || st == T_CHAR || st == T_BOOL) {
                /* Scalar subject: arms are literals, ranges, sets, or const
                 * names. `_` is REQUIRED for int/char (the domain is unbounded,
                 * so exhaustiveness is unprovable); a bool match is exhaustive
                 * when both values are covered. Duplicate or overlapping arms
                 * are an error (an earlier arm is dead). See
                 * docs/internals/design-scalar-match.md. */
                typedef struct { int64_t lo, hi; int line; } Iv;
                Iv *ivs = xmalloc((size_t)(s->narms * 8) * sizeof(Iv));
                int niv = 0;
                for (int i = 0; i < s->narms; i++) {
                    MatchArm *arm = &s->arms[i];
                    int m = vars_mark();
                    if (arm->variant && !strcmp(arm->variant, "_")) {
                        resolve_block(arm->body, arm->nbody, ret);
                        vars_restore(m);
                        continue;
                    }
                    if (arm->variant != NULL) {   /* a bare const name: `OP_LOAD:` */
                        Expr *c = consts_find(arm->variant);
                        if (c == NULL || c->kind != E_INT)
                            die_at(arm->line, "'%s' is not an int constant (match arms for %s are literals)",
                                   arm->variant, type_name(st));
                        arm->variant = NULL;      /* normalized for codegen */
                        arm->pn = 1; arm->pkind[0] = 0; arm->pcname[0] = NULL;
                        arm->plo[0] = arm->phi[0] = c->ival;
                    }
                    for (int k = 0; k < arm->pn; k++) {
                        int64_t lo = arm->plo[k], hi = arm->phi[k];
                        int kind = arm->pkind[k];
                        if (kind == 3) {          /* a const name element: fold now */
                            Expr *c = consts_find(arm->pcname[k]);
                            if (c == NULL || c->kind != E_INT)
                                die_at(arm->line, "'%s' is not an int constant", arm->pcname[k]);
                            lo = c->ival;
                            hi = lo;              /* single const: no range unless pch overrides */
                            kind = 0;             /* normalized: an int value */
                        }
                        if (arm->pch[k]) {        /* the range's high end is a const name */
                            Expr *d = consts_find(arm->pch[k]);
                            if (d == NULL || d->kind != E_INT)
                                die_at(arm->line, "'%s' is not an int constant", arm->pch[k]);
                            hi = d->ival;
                        }
                        arm->pkind[k] = kind;
                        arm->plo[k] = lo; arm->phi[k] = hi;   /* codegen reads these */
                        if (st == T_INT && kind != 0)
                            die_at(arm->line, "a match on an int takes int literal arms, not a %s",
                                   kind == 1 ? "char" : "bool");
                        if (st == T_CHAR && kind != 1)
                            die_at(arm->line, "a match on a char takes char literal arms");
                        if (st == T_BOOL && kind != 2)
                            die_at(arm->line, "a match on a bool takes true/false arms");
                        if (lo > hi)
                            die_at(arm->line, "a range starts at %lld and ends at %lld (the start must not exceed the end)",
                                   (long long)lo, (long long)hi);
                        ivs[niv].lo = lo; ivs[niv].hi = hi; ivs[niv].line = arm->line; niv++;
                    }
                    resolve_block(arm->body, arm->nbody, ret);
                    vars_restore(m);
                }
                /* dup/overlap: sort by lo, then any interval touching the
                 * previous one is a duplicate. Arm counts are tiny, so an
                 * insertion sort. */
                for (int i = 1; i < niv; i++)
                    for (int j = i; j > 0 && ivs[j].lo < ivs[j - 1].lo; j--) {
                        Iv t = ivs[j]; ivs[j] = ivs[j - 1]; ivs[j - 1] = t;
                    }
                for (int i = 1; i < niv; i++)
                    if (ivs[i].lo <= ivs[i - 1].hi)
                        die_at(ivs[i].line, "duplicate or overlapping match arm: [%lld..%lld] overlaps [%lld..%lld] from line %d",
                               (long long)ivs[i - 1].lo, (long long)ivs[i - 1].hi,
                               (long long)ivs[i].lo, (long long)ivs[i].hi, ivs[i - 1].line);
                if (!wild) {
                    if (st == T_INT || st == T_CHAR)
                        die_at(s->line, "a match on %s must carry a `_` arm (the domain is unbounded)",
                               type_name(st));
                    if (st == T_BOOL) {
                        int has0 = 0, has1 = 0;
                        for (int i = 0; i < niv; i++) {
                            if (ivs[i].lo <= 0 && 0 <= ivs[i].hi) has0 = 1;
                            if (ivs[i].lo <= 1 && 1 <= ivs[i].hi) has1 = 1;
                        }
                        if (!has0 || !has1)
                            die_at(s->line, "a match on a bool must cover both true and false, or carry a `_` arm");
                    }
                }
                free(ivs);
            } else if (st == T_STRING || st == T_BYTES || st == T_FLOAT) {
                die_at(s->line, "match on %s is refused: nothing in the tree dispatches on it -- use if/elif (see docs/internals/design-scalar-match.md D1)",
                       type_name(st));
            } else {
                die_at(s->line, "match expects an Option, Result, or enum value, or an int/char/bool, got %s", type_name(st));
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
        case S_FOR3: {
            /* The loop variable is scoped to the loop by exactly the mechanism
             * S_FORRANGE uses -- a vars_mark() before it is bound and a
             * vars_restore() after the body -- except that the binding is done
             * by resolving the init statement instead of by a hand-written
             * vars_push, so a typed init and an assign-only init behave as they
             * would anywhere else.
             *
             * The post clause is resolved AFTER the body block has been popped,
             * so it sees the loop scope and NOT the body's locals. That matches
             * where it runs and keeps `continue` honest: the jump to the post
             * clause skips the body's declarations, so a post clause allowed to
             * read one would read an uninitialized value. */
            int m = vars_mark();
            resolve_stmt(s->els[0], ret);
            if (resolve_expr(s->expr) != T_BOOL)
                die_at(s->line, "for condition must be bool");
            resolve_block(s->body, s->nbody - 1, ret);
            resolve_stmt(s->body[s->nbody - 1], ret);
            vars_restore(m);
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
                resolve_expr(s->r_stop)  != T_INT)
                die_at(s->line, "a counting `for` needs int bounds");
            /* HISTORY: a literal-zero-step refusal --
             *   if (s->r_step && s->r_step->kind == E_INT && s->r_step->ival == 0)
             *       die_at(s->line, "loop step is zero (the loop would never terminate)");
             * -- stood here until 2026-07-30, UNREACHABLE since 2026-07-29 because
             * `range(a,b,step)` was the only way to write a step and it is gone. The
             * guarantee does not survive into the three-clause form either: a post
             * clause is arbitrary code, so `for i := 0; i < n; i += 0:` cannot be
             * diagnosed. docs/spec/10-statements.md records that deliberate loss;
             * the field and this check went together in the loops-cleanup plan. */
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
                die_at(s->line, "can only index-assign an array element (strings and bytes are immutable)");
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
            const char *dn = NULL;
            if (!g_value_ctrl && s->expr) {
                dn = (s->expr->kind == E_CALL && is_pure_builtin(s->expr->sval))
                         ? s->expr->sval : discarded_map_get(s->expr);
                if (dn)
                    warn_at(s->expr->line, "result of `%s` is discarded; it has no side effects, so this statement does "
                                           "nothing (to change a map, use `m[k] = v` or `delete m[k]`)", dn);
            }
            Type et = resolve_expr(s->expr);
            if (!g_value_ctrl && IS_TASK(et))   /* CC-2: a discarded handle could never be waited */
                die_at(s->line, "a spawned task must be bound and waited (t := spawn f(...); ... wait(t))");
            /* A discarded Result silently swallows the error path. Not fatal (the no-side-effects
             * warning above already covers a pure discard), but nudge toward handling it. */
            if (!g_value_ctrl && !dn && IS_RES(et))
                warn_at(s->expr->line, "this Result is discarded, so its error is silently ignored; "
                                       "handle it with `match`, propagate with `or_return`, or bind it (x := ...)");
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
    if (is_array(t)) {
        int64_t sz = IS_ARRC(t) ? g_arrtypes[ARRC_ID(t)].size : 0;
        if (IS_BOUNDED(t)) return sfmt("bnd%lld_%s", (long long)sz, type_mangle_ident(arr_elem(t)));   /* distinct from a fixed [N]T of the same size/elem */
        if (sz > 0) return sfmt("arr%lld_%s", (long long)sz, type_mangle_ident(arr_elem(t)));   /* [N]T (1.6): distinct per fixed size */
        return sfmt("arr_%s", type_mangle_ident(arr_elem(t)));
    }
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

/* hashable(T): T is usable as a map key -- string, int (through newtypes), a
 * fieldless enum, or a composite (struct/tuple/array) of all-hashable leaves.
 * Mirrors the standalone key-validity path (map_of) without interning a type. */
static int key_type_ok(Type t) {
    if (base_of(t) == T_STRING) return 1;
    if (mapkey_intrep(t)) return 1;                 /* int (via newtype) or fieldless enum */
    return mapkey_composite(t) && key_hashable(t);
}

/* generics: the fixed, compiler-known `where` predicate set. Each is exactly the
 * capability the resolver already enforces (base_of sees through newtypes), so a
 * satisfied constraint guarantees the body's op type-checks for that T. */
static int constraint_ok(const char *pred, Type t) {
    Type b = base_of(t);
    if (!strcmp(pred, "numeric"))    return b == T_INT || b == T_FLOAT;                                   /* + - * / */
    if (!strcmp(pred, "comparable")) return b == T_INT || b == T_CHAR || b == T_FLOAT || b == T_STRING;   /* < > <= >= */
    if (!strcmp(pred, "has_str"))    return b == T_INT || b == T_BOOL || b == T_FLOAT || b == T_STRING;   /* str(x) */
    if (!strcmp(pred, "hashable"))   return key_type_ok(t);                                               /* map(T, _) key */
    if (!strcmp(pred, "defaultable")) return t == T_INT || t == T_FLOAT || t == T_BOOL || t == T_STRING;  /* zero$(T) -- exact scalars (no newtype) */
    return 1;   /* unknown predicates are rejected at parse */
}

/* 07-memory-model.md §11.5 -- the two parameter types that MUST NOT be `inout`,
 * as ONE rule with three callers: the concrete declaration, the generic
 * TEMPLATE declaration (its written types), and instantiate_generic (the
 * SUBSTITUTED types). The template check cannot subsume the instance one --
 * `inout $T` is not a channel or a function value until `$T` is bound -- and the
 * instance check cannot subsume the template one, since an uninstantiated
 * template is never monomorphized at all. `$T` itself trips neither predicate,
 * so no guard is needed; `Channel($T)` trips IS_CHAN and is rejected on purpose
 * (the rule is about the channel, not about T). */
static void check_inout_param_type(int line, Type t, int is_inout, const char *pname) {
    if (!is_inout) return;
    if (IS_CHAN(t))
        die_at(line, "a channel parameter cannot be inout (the handle is already shared)");
    if (IS_FUNC(t))
        die_at(line, "inout parameter '%s': a function value can't be inout "
               "(a callee could write a closure back into the caller and it would dangle)",
               pname);
}

static void instantiate_generic(Proc *gt, Expr *e) {
    if (e->nargs != gt->nparams)
        die_at(e->line, "'%s' takes %d argument(s), got %d", gt->name, gt->nparams, e->nargs);
    Type *binds = new_binds();
    int64_t *sizebinds = new_sizebinds();                         /* const generics 1.6B: sizebinds[sizeparam_id] = concrete N */
    int64_t *saved_sb = g_sizebinds; g_sizebinds = sizebinds;     /* match_type/subst_type bind & substitute `$N` while this is live */
    if (e->ntypeargs > 0) {   /* explicit call-site type args bind the params in declaration order */
        if (e->ntypeargs != gt->ntyparams)
            die_at(e->line, "'%s' has %d type parameter(s), but %d explicit type argument(s) were given",
                   gt->name, gt->ntyparams, e->ntypeargs);
        for (int i = 0; i < gt->ntyparams; i++)
            binds[(int)(gt->typarams[i] - T_TYPARAM_BASE)] = e->typeargs[i];
    }
    for (int j = 0; j < gt->nparams; j++) {
        g_in_arg++;                                   /* generic inference is an argument context too */
        Type at_ = resolve_expr(e->args[j]);          /* the concrete argument type */
        g_in_arg--;
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
    /* Phase 39: the declaration rules again, now on the SUBSTITUTED signature.
     * The template site (resolve_program) caught every WRITTEN concrete form;
     * this catches the bindings -- `fn f(c: inout $T)` called with a channel or
     * a function value -- which only exist here. Reported at the call, because
     * the call is what chose the binding that violates §11.5 / CC-4. */
    if (IS_CHAN(cret))
        die_at(e->line, "a function cannot return a channel -- create it in the owning scope and pass it down");
    for (int j = 0; j < gt->nparams; j++)
        check_inout_param_type(e->line, cparams[j], gt->params[j].is_inout, gt->params[j].name);
    e->sval = nm;                                     /* rewrite the call to the instance */
    if (sig_find(nm)) { g_sizebinds = saved_sb; return; }   /* already instantiated */
    Sig s; memset(&s, 0, sizeof s);
    s.name = nm; s.ret = cret; s.nparams = gt->nparams; s.builtin = 0;
    for (int j = 0; j < gt->nparams; j++) { s.params[j] = cparams[j]; s.inout[j] = gt->params[j].is_inout; s.sink[j] = gt->params[j].is_sink; s.variadic[j] = gt->params[j].is_variadic; }
    TBL_ENSURE(g_sigs, g_nsigs, g_sigs_cap); g_sigs[g_nsigs++] = s;
    TBL_ENSURE(g_ginsts, g_nginsts, g_nginsts_cap);
    GInst gi; gi.tmpl = gt; gi.name = nm; gi.nparams = gt->nparams; gi.ret = cret;
    gi.binds = (Type *)xmalloc((size_t)(g_ntyparams > 0 ? g_ntyparams : 1) * sizeof(Type));
    for (int i = 0; i < g_ntyparams; i++) gi.binds[i] = binds[i];
    gi.body = clone_block(gt->body, gt->nbody, gi.binds);   /* Stage-2: the instance's own `$T`-substituted body */
    gi.nbody = gt->nbody;
    for (int j = 0; j < gt->nparams; j++) gi.params[j] = cparams[j];
    gi.nsp = gt->nsizeparams;   /* const generics 1.6B: record this instance's `$N` values, for the body's int consts */
    for (int i = 0; i < gt->nsizeparams; i++)
        gi.spvals[i] = sizebinds[sizeparam_id(sizeparam_enc(gt->sizeparams[i]))];
    g_sizebinds = saved_sb;
    g_ginsts[g_nginsts++] = gi;
    if (g_nginsts > 1024)   /* runaway guard: fail closed on a recursive generic at a strictly-growing type */
        die_at(e->line, "too many generic instantiations (> 1024) -- a recursive generic at a growing type?");
}

/* ---- CC-6: channel liveness lint ----------------------------------------
 * Value semantics removes data races; it does not remove deadlock
 * (docs/guides/concurrency.md). The provable half of deadlock IS decidable
 * here, because a channel handle can only reach code by being passed as an
 * argument -- it cannot be returned (CC-4, resolve_program below), stored in a
 * struct/enum/newtype/container, captured by a closure, or rebound. So the set
 * of procs that can touch one channel is a CLOSED graph rooted at its decl.
 *
 * That is what makes an ABSENCE argument sound: if no `send` appears anywhere
 * in that graph, no execution can ever send, whatever the control flow does.
 * Every warning here is of that shape -- an operation that appears NOWHERE --
 * never "this path might not send". Anything the walk cannot follow (an unknown
 * or generic callee, a variadic call) sets `opaque` and silences every warning
 * for that channel: a missed deadlock is a bug the programmer already had, a
 * false warning is one I handed them.
 *
 * Warning, not error, deliberately: rejecting these would change the set of
 * accepted programs, which is a language change and belongs in the spec. */
typedef struct {
    int send, recv, close;   /* an op of this kind exists SOMEWHERE in the graph */
    int recv_block;          /* a receive that can PARK: recv(ch), or a select arm with no `default:`
                              * (a select with `default:` polls -- tests/conc/select.ty does exactly
                              * this on a deliberately senderless channel, and must not warn) */
    int opaque;              /* the walk hit something it cannot follow -- stay quiet */
    int sel_closed_line;     /* a `select` on this channel with a `closed:` arm (0 = none) */
} ChanUse;

#define CHAN_VISIT_MAX 256
typedef struct { const Proc *pr; const char *nm; } ChanVisit;
static ChanVisit g_cvisit[CHAN_VISIT_MAX];
static int g_ncvisit = 0;

static void chan_scan_body(ProcVec *prog, Stmt **body, int n, const char *nm, ChanUse *u);

static Proc *chan_proc_find(ProcVec *prog, const char *name) {
    for (int i = 0; i < prog->n; i++)
        if (!prog->v[i]->generic && !prog->v[i]->is_extern && !strcmp(prog->v[i]->name, name))
            return prog->v[i];
    return NULL;   /* builtin, extern, generic template, or package-mangled: caller marks opaque */
}

/* follow the handle into a callee, under the parameter name it arrives as */
static void chan_walk_proc(ProcVec *prog, Proc *pr, const char *nm, ChanUse *u) {
    for (int i = 0; i < g_ncvisit; i++)   /* recursion / diamond: already accounted for */
        if (g_cvisit[i].pr == pr && !strcmp(g_cvisit[i].nm, nm)) return;
    if (g_ncvisit >= CHAN_VISIT_MAX) { u->opaque = 1; return; }
    g_cvisit[g_ncvisit].pr = pr; g_cvisit[g_ncvisit].nm = nm; g_ncvisit++;
    chan_scan_body(prog, pr->body, pr->nbody, nm, u);
}

static int chan_is(Expr *e, const char *nm) {
    return e && e->kind == E_IDENT && e->sval && !strcmp(e->sval, nm);
}

static void chan_scan_expr(ProcVec *prog, Expr *e, const char *nm, ChanUse *u);

static void chan_scan_call(ProcVec *prog, Expr *e, const char *nm, ChanUse *u) {
    int passes = 0;
    for (int i = 0; i < e->nargs; i++) if (chan_is(e->args[i], nm)) passes = 1;
    if (!passes) return;   /* this call doesn't take our channel; args still scanned by the caller */
    if (e->sval && e->nargs >= 1 && chan_is(e->args[0], nm)) {   /* the three builtin ops (UFCS is rewritten to this form) */
        if (!strcmp(e->sval, "send"))  { u->send  = 1; return; }
        if (!strcmp(e->sval, "recv"))  { u->recv  = 1; u->recv_block = 1; return; }
        if (!strcmp(e->sval, "close")) { u->close = 1; return; }
    }
    Proc *cal = e->sval ? chan_proc_find(prog, e->sval) : NULL;
    if (!cal || e->nargs > cal->nparams) { u->opaque = 1; return; }
    for (int i = 0; i < e->nargs; i++)
        if (chan_is(e->args[i], nm)) {
            if (cal->params[i].is_variadic) { u->opaque = 1; return; }
            chan_walk_proc(prog, cal, cal->params[i].name, u);
        }
}

static void chan_scan_expr(ProcVec *prog, Expr *e, const char *nm, ChanUse *u) {
    if (!e) return;
    if (e->kind == E_CALL) chan_scan_call(prog, e, nm, u);
    chan_scan_expr(prog, e->lhs, nm, u);   /* E_SPAWN carries its E_CALL here */
    chan_scan_expr(prog, e->rhs, nm, u);
    for (int i = 0; i < e->nargs; i++) chan_scan_expr(prog, e->args[i], nm, u);
}

static void chan_scan_stmt(ProcVec *prog, Stmt *s, const char *nm, ChanUse *u) {
    if (!s) return;
    chan_scan_expr(prog, s->expr, nm, u);
    chan_scan_expr(prog, s->target, nm, u);
    chan_scan_expr(prog, s->r_start, nm, u);
    chan_scan_expr(prog, s->r_stop, nm, u);
    /* no r_step: every S_FORRANGE steps by 1 (the loops-cleanup plan). */
    if (s->kind == S_SELECT) {
        int mine = 0, closed_arm = 0, dflt = 0;
        for (int a = 0; a < s->narms; a++) {
            if (s->sel_ch && s->sel_ch[a]) {
                chan_scan_expr(prog, s->sel_ch[a], nm, u);
                if (chan_is(s->sel_ch[a], nm)) { u->recv = 1; mine = 1; }
            }
            if (s->arms[a].variant && !strcmp(s->arms[a].variant, "closed"))  closed_arm = 1;
            if (s->arms[a].variant && !strcmp(s->arms[a].variant, "default")) dflt = 1;
        }
        if (mine && !dflt) u->recv_block = 1;
        /* `parallel for x in ch` desugars to exactly this (resolve_parfor), so this
         * is also how the documented "workers park unless the producer closes" hang
         * is caught. The arm fires only when EVERY listed channel is closed+drained,
         * so one never-closed channel is enough to make it dead. */
        if (mine && closed_arm && !u->sel_closed_line) u->sel_closed_line = s->line;
    }
    chan_scan_body(prog, s->body, s->nbody, nm, u);
    chan_scan_body(prog, s->els, s->nels, nm, u);
    for (int a = 0; a < s->narms; a++)
        chan_scan_body(prog, s->arms[a].body, s->arms[a].nbody, nm, u);
    if (s->ctrl) chan_scan_stmt(prog, s->ctrl, nm, u);
}

static void chan_scan_body(ProcVec *prog, Stmt **body, int n, const char *nm, ChanUse *u) {
    for (int i = 0; i < n; i++) chan_scan_stmt(prog, body[i], nm, u);
}

static void chan_check_decl(ProcVec *prog, Proc *owner, Stmt *d) {
    ChanUse u; memset(&u, 0, sizeof u);
    g_ncvisit = 0;
    chan_scan_body(prog, owner->body, owner->nbody, d->name, &u);   /* the decl's own proc, then outward */
    if (u.opaque) return;
    diag_use_proc(owner);   /* package mode: name the file this channel was declared in */
    if (u.recv_block && !u.send)
        warn_at(d->line, "nothing ever sends on channel '%s', so a receive on it parks forever", d->name);
    else if (u.send && !u.recv)
        warn_at(d->line, "nothing ever receives from channel '%s', so a send parks once its buffer fills", d->name);
    if (u.sel_closed_line && !u.close)
        warn_at(u.sel_closed_line, "close(%s) is never called, so this `closed:` arm can never run "
                "(a `parallel for` over a channel ends only when it is closed)", d->name);
}

static void chan_find_decls(ProcVec *prog, Proc *owner, Stmt **body, int n) {
    for (int i = 0; i < n; i++) {
        Stmt *s = body[i];
        if (!s) continue;
        if (s->kind == S_DECL && IS_CHAN(s->decl_type) && s->name) chan_check_decl(prog, owner, s);
        chan_find_decls(prog, owner, s->body, s->nbody);
        chan_find_decls(prog, owner, s->els, s->nels);
        for (int a = 0; a < s->narms; a++)
            chan_find_decls(prog, owner, s->arms[a].body, s->arms[a].nbody);
    }
}

static void check_channel_liveness(ProcVec *prog) {
    for (int i = 0; i < prog->n; i++)
        if (!prog->v[i]->generic && !prog->v[i]->is_extern)
            chan_find_decls(prog, prog->v[i], prog->v[i]->body, prog->v[i]->nbody);
}

/* Package mode: two definitions of one name are each innocent in their own file,
 * and the "already defined" diagnostic below fires at the SECOND one, so it names
 * whichever file sorts later (scan_pkg_files qsorts, `:11759`). For two unrelated
 * scratch programs sitting in one directory that is routinely the entry file the
 * user actually named -- the message then blames the file it was asked to build
 * and never mentions that a sibling was compiled at all. FRICTION.md records four
 * compile cycles spent working out that the fix was `mkdir`.
 *
 * The directory scan is NOT the bug and is not changed: a package may legally
 * span files (`tests/pkg/multifile/`), and it is entered only when the entry file
 * declares a `package` header (`:12069-12071`) -- which a scratch program must do
 * to `import` anything. So the fix is the diagnostic: name the other definition.
 * Returns "<file>:<line>" for a same-named proc in a DIFFERENT file, else NULL
 * (a same-file duplicate is self-evident and keeps the plain message). */
static const char *dup_other_file(ProcVec *prog, int self, const char *name) {
    const char *mine = prog->v[self]->srcfile;
    for (int j = 0; j < prog->n; j++) {
        Proc *o = prog->v[j];
        if (j == self || !o->srcfile || strcmp(o->name, name) != 0) continue;
        if (mine && strcmp(o->srcfile, mine) == 0) continue;
        return sfmt("%s:%d", o->srcfile, o->line);
    }
    return NULL;
}
__attribute__((noreturn))
static void die_dup_proc(ProcVec *prog, int self, const char *name) {
    const char *other = dup_other_file(prog, self, name);
    if (other)
        die_at(prog->v[self]->line,
               "'%s' is already defined -- also at %s, a DIFFERENT file in the same package: "
               "tychoc compiles every .ty beside the entry file, so two unrelated programs "
               "cannot share one directory", name, other);
    die_at(prog->v[self]->line, "'%s' is already defined", name);
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
    /* const generics 1.6B: a `[$N]T` size parameter is meaningful only in a generic
     * function parameter (the argument fixes N). In a stored position nothing infers
     * N, so reject it -- fail closed rather than emit a phantom array type. */
    for (int i = 0; i < g_nstructs; i++)
        for (int f = 0; f < g_structs[i].nfields; f++)
            if (type_has_sizeparam(g_structs[i].fields[f].type))
                die_at(g_structs[i].line, "a struct field cannot be a `[$N]T` size-parameterized array -- use a fixed `[N]T` or a dynamic `[T]`");
    for (int i = 0; i < g_nenums; i++)
        for (int v = 0; v < g_enums[i].nvariants; v++)
            for (int p = 0; p < g_enums[i].variants[v].npayload; p++)
                if (type_has_sizeparam(g_enums[i].variants[v].payload[p]))
                    die_at(g_enums[i].line, "an enum payload cannot be a `[$N]T` size-parameterized array -- use a fixed `[N]T` or a dynamic `[T]`");
    for (int i = 0; i < g_nnewtypes; i++)
        if (type_has_sizeparam(g_newtypes[i].under))
            die_at(0, "a newtype cannot wrap a `[$N]T` size-parameterized array");
    /* register every user proc up front so calls can be forward refs */
    for (int i = 0; i < prog->n; i++) {
        Proc *pr = prog->v[i];
        diag_use_proc(pr);   /* package mode: name THIS proc's file in any error below */
        /* Phase 39 -- these three are rules about the DECLARATION (11-functions.md
         * §15.1 arity; 07-memory-model.md §11.5 inout; CC-4 channel return), so they
         * run BEFORE the generic stash below. That `continue` exists only to skip Sig
         * REGISTRATION ("not a callable Sig"), never to defer validation, and letting
         * them sit after it meant `fn f(c: inout Channel(int), p: $T)` compiled and ran
         * on a technicality. Written types only; instantiate_generic re-runs the inout
         * and channel-return rules on the substituted ones.
         * The arity check MUST come first for a template: instantiate_generic builds
         * `Type cparams[16]`, so a 17-parameter generic overran that stack array
         * (UBSan, before this move: "src/tychoc.c:6868: index 16 out of bounds for
         * type 'Type [16]'") and then emitted a nonsense arity diagnostic. */
        if (pr->nparams > 16) die_at(pr->line, "too many parameters (max 16)");
        if (IS_CHAN(pr->ret))
            die_at(pr->line, "a function cannot return a channel -- create it in the owning scope and pass it down");
        for (int j = 0; j < pr->nparams; j++)
            check_inout_param_type(pr->line, pr->params[j].type, pr->params[j].is_inout, pr->params[j].name);
        if (pr->generic) {   /* a `$T` template: not a callable Sig -- stash it; instances are made per call */
            if (sig_find(pr->name) || generic_find(pr->name) || consts_find(pr->name))
                die_dup_proc(prog, i, pr->name);
            TBL_ENSURE(g_generics, g_ngenerics, g_generics_cap);
            g_generics[g_ngenerics++] = pr;
            continue;
        }
        if (sig_find(pr->name) || consts_find(pr->name))
            die_dup_proc(prog, i, pr->name);
        Sig s; memset(&s, 0, sizeof s);
        s.name = pr->name; s.ret = pr->ret; s.nparams = pr->nparams; s.builtin = 0;
        s.is_extern = pr->is_extern;
        if (pr->is_extern) add_link(pr->lib);   /* FFI: collect -lLib for the cc line */
        /* the arity cap and the two `inout` type rules moved above the generic
         * stash (Phase 39) -- s.params[16] is safe to fill because pr->nparams
         * was already capped there. */
        for (int j = 0; j < pr->nparams; j++) {
            s.params[j] = pr->params[j].type;
            s.inout[j]  = pr->params[j].is_inout;
            s.sink[j] = pr->params[j].is_sink;
            s.variadic[j] = pr->params[j].is_variadic;
            /* inout: non-heap types (int/bool/pure struct) and the mutable
             * aggregates [int]/[string]/heap-bearing structs. A heap inout
             * carries its value's owning arena (_ina_<name>), so any
             * allocating mutation (element copy, field copy, regrow/push)
             * lands in the caller's arena where the value lives. `string`
             * rides the same machinery: the value itself is immutable, but
             * REASSIGNMENT through the borrow (s = s + ".") reaches the
             * caller, and the new bytes build in _ina_<name>. */
        }
        TBL_ENSURE(g_sigs, g_nsigs, g_sigs_cap);
        g_sigs[g_nsigs++] = s;
    }
    Sig *m = sig_find("main");
    if (!m) { fprintf(stderr, "%s: error: no 'main' procedure\n", g_srcname); exit(1); }
    for (int i = 0; i < prog->n; i++) {
        Proc *pr = prog->v[i];
        diag_use_proc(pr);   /* package mode: body-resolve errors name THIS proc's file */
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
             * EXCEPT an inout one, which is a by-pointer share the callee may
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
        /* fall-off-the-end lint: a non-void proc whose body can reach its end
         * without a return silently yields a zero value (codegen emits a
         * defensive `return (T){0}`). Warn rather than reject — a body ending
         * in an infinite loop never falls through yet isn't provably total. */
        if (pr->ret != T_VOID && !block_ends_in_return(pr->body, pr->nbody))
            warn_at(pr->line, "not all paths of '%s' return a value (a fall-off-the-end yields a zero value)", pr->name);
    }
    check_channel_liveness(prog);   /* CC-6: after every body is resolved (parallel-for desugars are in place) */
}

/* ------------------------------------------------------------- codegen */
/* --- bounds-check elision for monotone loop indices -----------------------
 * Inside `for i in range(len(A)):` (start 0, step +1), the access `A[i]` is
 * provably in [0, len(A)): the C loop caches `_stop = len(A)` once at entry,
 * `len` is an un-redefinable builtin returning the true length, Tycho has NO
 * in-place array-shrink op, and we verify below that the loop body never
 * reassigns/shadows A or i and never passes A whole to a call (push / a
 * possibly-inout callee could change it). So the per-element bounds check is
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
    if (expr_passes_arr(s->r_start, arr) || expr_passes_arr(s->r_stop, arr))
        return 1;
    if (stmts_unsafe(s->body, s->nbody, iv, arr)) return 1;
    if (stmts_unsafe(s->els,  s->nels,  iv, arr)) return 1;
    for (int a = 0; a < s->narms; a++) {
        for (int b = 0; b < s->arms[a].nsubbinds; b++)
            if (!strcmp(s->arms[a].subbinds[b], iv) || !strcmp(s->arms[a].subbinds[b], arr)) return 1;
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

/* --- the three-clause form's elidable shape -------------------------------
 * `for i := 0; i < len(A); i += 1:` is the S_FOR3 spelling of what
 * `for i in range(len(A)):` used to be, and it is elidable for a slightly
 * STRONGER reason than S_FORRANGE's: S_FORRANGE caches `_stop = len(A)` once
 * before the loop and leans on the body never shrinking A, whereas S_FOR3
 * emits the condition into the C `while (...)` header (src/tychoc.c:10798), so
 * `i < len(A)` is re-evaluated on every iteration and holds at the top of each
 * body by construction. What still has to be PROVED is the rest of the shape.
 * Unlike S_FORRANGE, where start/stop/step are three separate AST fields, here
 * the three parts are an init statement, a condition expression and a post
 * statement, so each is matched explicitly and anything else falls through to
 * the checked accessor. Fail closed -- a wrong elision is a memory-safety bug.
 *
 *   init  s->els[0]           S_DECL, name = i, value the literal 0
 *   cond  s->expr             E_BINOP '<' (strictly), lhs = IDENT i,
 *                             rhs = len(A) with A a bare IDENT
 *   post  s->body[nbody-1]    S_ASSIGN to i, value E_BINOP '+' (IDENT i, 1)
 *
 * So `i <= len(A)`, `i += 2`, `i -= 1`, `i := 1`, `i < n`, `i < len(f(a))` and
 * a post that assigns anything but i all keep `tycho_arr_*_get`. The body guard
 * is the SAME `stmts_unsafe` S_FORRANGE uses, run over the body WITHOUT its
 * last element: the post clause lives there (see the `els`/`body` note at
 * src/tychoc.c:1565) and assigns i, so including it would report unsafe every
 * time. Returns the array's name, or NULL when the shape is not certain.
 *
 * WHAT THIS BUYS, MEASURED -- read before "improving" it. At -O3, the level
 * tychoc itself hands to cc, this function buys ~NOTHING: gcc already folds the
 * accessor's `i >= xs.len` test, because the three-clause form emits `h_i <
 * ((h_xs).len)` into the C `while` header and that is the exact fact VRP needs.
 * Best-of-3 on a scan loop, elided vs checked: -O3 207 vs 208 ms (1.00x), -O2
 * 1.14x, -O1 1.88x. Four shapes were tried looking for one gcc could not fold
 * (flat scan, in-place write, nested cross product, `inout [int]` parameter);
 * none separated. bench/guard.sh:49-62 carries the second measurement and is why
 * that lane asserts the emitted C STRUCTURALLY instead of a wall-time ratio.
 * It is KEPT anyway, deliberately: it is the only thing that elides at -O0/-O1,
 * which is what `tychoc -g` builds (src/tychoc.c:12754) and what a debugger step
 * actually runs. Deleting it is a live option (the loops-cleanup plan option (b)) but
 * NOT on these numbers alone -- they are one machine and one gcc, and the
 * measurement must be repeated on a second toolchain first. Note the historical
 * asymmetry that makes deletion thinkable at all: the old `S_FORRANGE` spelling
 * cached `_stop` before the loop (src/tychoc.c:10885) and broke the link to
 * `len`, which is exactly why this elision had to be written by hand. */
static const char *for3_elidable_arr(Stmt *s) {
    if (!elision_on() || s->nels != 1 || s->nbody < 1 || g_nelide >= 64) return NULL;
    Stmt *init = s->els[0], *post = s->body[s->nbody - 1];
    Expr *cond = s->expr;
    /* init: `i := 0` (a typed `i: int = 0` is the same S_DECL and also matches) */
    if (!init || init->kind != S_DECL || !init->name || init->ctrl) return NULL;
    if (!init->expr || init->expr->kind != E_INT || init->expr->ival != 0) return NULL;
    const char *iv = init->name;
    /* cond: `i < len(A)` */
    if (!cond || cond->kind != E_BINOP || cond->op != TK_LT) return NULL;
    if (!cond->lhs || cond->lhs->kind != E_IDENT || strcmp(cond->lhs->sval, iv)) return NULL;
    Expr *bound = cond->rhs;
    if (!bound || bound->kind != E_CALL || !bound->sval || strcmp(bound->sval, "len") ||
        bound->nargs != 1 || !bound->args[0] || bound->args[0]->kind != E_IDENT) return NULL;
    if (IS_BOUNDED(bound->args[0]->type)) return NULL;   /* bounded stores in .v, not .data — elision emits .data[i], so never elide it */
    /* post: `i += 1` exactly (parsed as `i = i + 1`, src/tychoc.c:3554-3559) */
    if (!post || post->kind != S_ASSIGN || !post->name || strcmp(post->name, iv)) return NULL;
    Expr *inc = post->expr;
    if (!inc || inc->kind != E_BINOP || inc->op != TK_PLUS) return NULL;
    if (!inc->lhs || inc->lhs->kind != E_IDENT || strcmp(inc->lhs->sval, iv)) return NULL;
    if (!inc->rhs || inc->rhs->kind != E_INT || inc->rhs->ival != 1) return NULL;
    const char *arr = bound->args[0]->sval;
    if (!strcmp(arr, iv)) return NULL;
    if (stmts_unsafe(s->body, s->nbody - 1, iv, arr)) return NULL;
    return arr;
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
static const char *g_cur_proc_name = "?";
static int g_loop_lbl = 0;   /* per-proc loop counter for the labels -- STABLE under re-formatting, unlike a line number */              /* current proc name, for per-scope arena labels ("fn:loopN") */

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
/* Depth of aggregate constructions nested inside a user-proc call's argument
 * expressions. A deep copy of a live heap local into such an aggregate is the
 * by-value call-argument copy -- the copy the Val-style diagnostic names. At a
 * plain declaration (`t := (x, 7)`) the same copy is the assignment the user
 * wrote, so it stays silent (that site is the separate `b := a` decision).
 * Nested calls nest the counter, so `f(g((s, 1)))` still sees a call arg. */
static int g_call_arg_depth = 0;
/* Depth of a return statement's expression. At a return the value is built in
 * the caller's arena (`_parent`), never the local's, and the advice "make this
 * its last use" means deleting the local's OTHER reads elsewhere in the proc
 * -- not locally actionable, so copies at returns stay silent. Without this
 * gate the warning fired ~15x on the self-hosted compiler alone, all at
 * `return type_of(ECall(v, args, ...))` AST-building sites where the copy is
 * structurally unavoidable and every fire is noise. */
static int g_in_return = 0;

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
        c += count_reads_e(s->r_start, nm) + count_reads_e(s->r_stop, nm);
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
        c += expr_pushcount(s->r_start, nm) + expr_pushcount(s->r_stop, nm);
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
            for (int b = 0; b < s->arms[a].nsubbinds; b++) if (!strcmp(s->arms[a].subbinds[b], nm)) return 1;
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
            && is_array(e->args[0]->type)
            && !IS_BOUNDED(e->args[0]->type)) {   /* any array element family (int/float/str/composite); soa + inout + bounded (no grow) excluded */
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
        fprintf(o, "%s*_fd%d = h_%s.data; tycho_int _fl%d = h_%s.len, _fc%d = h_%s.cap;\n",
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
        if (expr_mutates(s->r_start, nm) || expr_mutates(s->r_stop, nm))
            return 1;
        if (block_mutates(s->body, s->nbody, nm)) return 1;
        if (block_mutates(s->els, s->nels, nm)) return 1;
        for (int a = 0; a < s->narms; a++)
            if (block_mutates(s->arms[a].body, s->arms[a].nbody, nm)) return 1;
        if (s->ctrl && block_mutates(&s->ctrl, 1, nm)) return 1;   /* value if/match decl: a tail may pass &nm to an inout param */
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

/* --- return-only escape (precise, intra-procedural) ----------------------
 * The bare `return x` escape set (collect_escapes) misses a local EMBEDDED in a
 * returned constructor (`return Bin(k, l, r)`), so a recursive tree/list builder
 * re-homes its children on every return (O(n*depth) deep copies). We add such a
 * local to the escape set ONLY when it provably ALWAYS becomes part of the
 * returned value — never built-and-discarded — so promoting it to _parent costs
 * zero retention (the failure mode of the naive "any ident in any return"). The
 * test is entirely local to one function body (no call graph): a top-level heap
 * decl x qualifies iff (0) the fn returns a heap type, (1) x is read at least
 * once, (2) EVERY read of x is inside a return expression, and (3) EVERY return
 * after x's declaration contains x. gen's `l`/`r` pass (only ever returned, on
 * both paths); fold's `fl` fails (read by is_lit, discarded when folded away). */
static const char **g_ret_alias; static int g_ret_alias_cap = 0; static int g_nret_alias = 0;
static int in_ret_alias(const char *nm) {
    for (int i = 0; i < g_nret_alias; i++)
        if (!strcmp(g_ret_alias[i], nm)) return 1;
    return 0;
}
static int reads_in_returns_b(Stmt **body, int n, const char *nm) {
    int c = 0;
    for (int i = 0; i < n; i++) { Stmt *s = body[i];
        if (s->kind == S_RETURN && s->expr) c += count_reads_e(s->expr, nm);
        c += reads_in_returns_b(s->body, s->nbody, nm) + reads_in_returns_b(s->els, s->nels, nm);
        for (int a = 0; a < s->narms; a++) c += reads_in_returns_b(s->arms[a].body, s->arms[a].nbody, nm);
    }
    return c;
}
/* 0 iff some return in the subtree does NOT read nm (a bare/void return, or an
 * expression without nm — i.e. nm is discarded on that path); else 1. */
static int every_return_has(Stmt **body, int n, const char *nm) {
    for (int i = 0; i < n; i++) { Stmt *s = body[i];
        if (s->kind == S_RETURN) {
            if (!s->expr || count_reads_e(s->expr, nm) == 0) return 0;
        }
        if (!every_return_has(s->body, s->nbody, nm)) return 0;
        if (!every_return_has(s->els, s->nels, nm)) return 0;
        for (int a = 0; a < s->narms; a++)
            if (!every_return_has(s->arms[a].body, s->arms[a].nbody, nm)) return 0;
    }
    return 1;
}
/* Populate g_esc (+ g_ret_alias) with the return-only-escaping top-level locals
 * of pr, per the rule above. Additive to collect_escapes' bare-return set. */
static void collect_ret_alias(Proc *pr) {
    g_nret_alias = 0;
    if (!type_is_heap(pr->ret)) return;
    for (int i = 0; i < pr->nbody; i++) {
        Stmt *s = pr->body[i];
        if (s->kind != S_DECL || !s->name || !type_is_heap(s->decl_type)) continue;
        int tot = count_reads_b(pr->body, pr->nbody, s->name);
        int inr = reads_in_returns_b(pr->body, pr->nbody, s->name);
        if (tot > 0 && tot == inr
            && every_return_has(pr->body + i + 1, pr->nbody - i - 1, s->name)) {
            TBL_ENSURE(g_esc, g_nesc, g_esc_cap); g_esc[g_nesc++] = s->name;
            TBL_ENSURE(g_ret_alias, g_nret_alias, g_ret_alias_cap); g_ret_alias[g_nret_alias++] = s->name;
        }
    }
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
    /* T_BYTES too: same length-headered buffer, so tycho_str_append grows it by the
     * same rules and the uniqueness argument is unchanged. Without this, a
     * byte-at-a-time build (`out = out + b[i:i+1]`) would be O(n^2). */
    if (!(e->kind == E_BINOP && e->op == TK_PLUS && (e->type == T_STRING || e->type == T_BYTES))) return 0;
    for (Expr *cur = e; cur->kind == E_BINOP && cur->op == TK_PLUS && (cur->type == T_STRING || cur->type == T_BYTES); cur = cur->lhs)
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
        case T_BYTES:        return sfmt("tycho_str_copy(%s, %s)", arena, val);   /* bytes shares string's length-headered buffer -> same length-safe re-home (was missing: a bytes field of a returned/stored aggregate wasn't re-homed and dangled) */
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
/* The ZERO-COST reinterprets (`:8745`) return their argument's pointer
 * unchanged -- they are casts, not constructions. So `to_str(b)` over a place is
 * itself a place: it reads existing storage and binding/returning it MUST copy.
 * Reported as a call, it used to slip past both is_place and ret_must_copy, and
 * `out := to_str(b); return out` over a _scope-owned bytes local returned a
 * dangling pointer (measured: "got=[8]" for a buffer holding "ABCDEFGH").
 * `to_bytes([int])` is excluded -- that one really does allocate (`:8743`). */
static int is_reinterpret_of_place(Expr *e);
static int is_place(Expr *e) {
    return e->kind == E_IDENT || e->kind == E_FIELD || e->kind == E_INDEX
        || e->kind == E_TUPIDX || e->kind == E_SLICE   /* a slice aliases its source; binding it copies */
        || is_reinterpret_of_place(e);
}
static int is_reinterpret_of_place(Expr *e) {
    if (e->kind != E_CALL || e->nargs != 1 || e->lhs || !e->sval) return 0;
    if (strcmp(e->sval, "to_str") && strcmp(e->sval, "to_bytes") && strcmp(e->sval, "to_under")) return 0;
    if (!strcmp(e->sval, "to_bytes") && base_of(e->args[0]->type) == T_ARRAY_INT) return 0;   /* a real conversion */
    return is_place(e->args[0]);
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
    /* see is_reinterpret_of_place: `return to_str(x)` is `return x` with a
     * different static type, so ask the same question of x (a _parent-owned
     * local still needs no copy -- the reinterpret does not change where it lives) */
    if (is_reinterpret_of_place(e)) return ret_must_copy(e->args[0]);
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
        if (!self_move && !can_move_from(arg, arena)) {
            /* Val-style copy diagnostic: this deep copy is unavoidable AND
             * observable -- arg is a heap-bearing local still live after this
             * point (read again somewhere in the proc), so move-on-last-use
             * cannot hand off its buffer. Gated to fire ONLY when the
             * aggregate is itself a call argument (g_call_arg_depth): at a
             * plain declaration the copy is the assignment the user wrote,
             * and the corpus idiom builds aggregates from live locals there
             * on purpose (value-semantics fixtures). Only a BARE local gets
             * the warning: a field/index can never be moved (you cannot take
             * a part out of a value), a param borrows the caller's buffer,
             * and inside a loop even a last-use local must copy every
             * iteration -- in all three the advice below is not actionable,
             * so the copy stays silent. The same-arena test keeps it honest:
             * a last-use local in a DIFFERENT arena also cannot be moved,
             * and "make this its last use" would be wrong advice there.
             * (Mirrors the sink-param die at sink_arg_into; a warning here,
             * not a die, because the copy is value-semantics-correct -- the
             * user just paid for it silently.) */
            if (arg->kind == E_IDENT && !is_param(arg->sval)
                && g_loop_depth == 0 && g_call_arg_depth > 0 && g_in_return == 0
                && cv_arena(arg->sval) && !strcmp(cv_arena(arg->sval), arena))
                warn_at(arg->line, "unavoidable copy of '%s' into this aggregate (it is still live "
                                   "after this point); make this its last use, or pass a copy you "
                                   "keep (`y := %s`)", arg->sval, arg->sval);
            v = copy_into(t, arena, v);
        }
    }
    return v;
}

/* Construction-arg alias (generalizes the return-slot move to embedded locals).
 * Skip the redundant deep-copy when `arg` is a return-only-escaping local
 * already living in _parent (its whole value is where the caller will read it)
 * AND it occurs exactly ONCE among `args` — so no two slots of the fresh
 * aggregate ever share one buffer (which would break value semantics on a later
 * mutation). Sound because such a local is provably dead after the return (it is
 * read only inside returns, and every return contains it — collect_ret_alias).
 * `args`/`nargs` are the sibling args of the SAME aggregate; pass NULL for a
 * single-payload construction (Some/Ok/Err), trivially unique. Falls back to
 * arg_into. Applies only when constructing into _parent (a returned value). */
static char *alias_arg(Type t, const char *arena, Expr *arg, Expr **args, int nargs) {
    if (arena && !strcmp(arena, "_parent") && arg->kind == E_IDENT && arg->sval
        && in_ret_alias(arg->sval) && cv_in_parent(arg->sval)) {
        int occ = 0;
        if (!args) occ = 1;
        else for (int j = 0; j < nargs; j++)
            if (args[j]->kind == E_IDENT && args[j]->sval && !strcmp(args[j]->sval, arg->sval)) occ++;
        if (occ == 1) return gen_expr(arg, arena);   /* alias: elide the deep copy */
    }
    return arg_into(t, arena, arg);
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
 * byte; arrays element-wise; structs field-wise via a generated tycho_eq_S_X.
 * Recurses through nesting exactly as the deep copy does. */
/* deep hash of a value `v` of type t, for composite map keys. Mirrors gen_eq's
 * structure; folds field hashes (the struct body does the fold). int/bool/char ->
 * the seeded SplitMix64 int hash; string/bytes -> keyed SipHash; float -> hash its
 * bit pattern; a fieldless enum -> its tag; a nested struct -> its own hash. Equal
 * values (by deep ==) always hash equal. */
static int g_hash_det = 0;   /* deterministic mode for gen_hash / the dhash_* families (the public hash(x)) */
static char *gen_hash(Type t, const char *v) {
    t = base_of(t);
    if (t == T_STRING || t == T_BYTES) return sfmt(g_hash_det ? "tycho_si_hash_det(%s)" : "tycho_si_hash(%s)", v);
    if (t == T_FLOAT)  return sfmt(g_hash_det ? "tycho_ik_hash_det((tycho_int)((union { double _d; tycho_int _l; }){ ._d = (%s) })._l)"
                                                       : "tycho_ik_hash((tycho_int)((union { double _d; tycho_int _l; }){ ._d = (%s) })._l)", v);
    if (enum_fieldless(t)) return sfmt(g_hash_det ? "tycho_ik_hash_det((tycho_int)((%s)->tag))" : "tycho_ik_hash((tycho_int)((%s)->tag))", v);
    if (IS_STRUCT(t))  return sfmt(g_hash_det ? "tycho_dhash_S_%s(%s)" : "tycho_hash_S_%s(%s)", g_structs[STRUCT_ID(t)].name, v);
    if (IS_TUP(t))     return sfmt(g_hash_det ? "tycho_dhash_T%d(%s)" : "tycho_hash_T%d(%s)", TUP_ID(t), v);
    if (t == T_ARRAY_INT)    return sfmt(g_hash_det ? "tycho_arr_int_hash_det(%s)" : "tycho_arr_int_hash(%s)", v);
    if (t == T_ARRAY_STRING) return sfmt(g_hash_det ? "tycho_arr_str_hash_det(%s)" : "tycho_arr_str_hash(%s)", v);
    if (t == T_ARRAY_FLOAT)  return sfmt(g_hash_det ? "tycho_arr_float_hash_det(%s)" : "tycho_arr_float_hash(%s)", v);
    if (IS_ARRC(t))    return sfmt(g_hash_det ? "tycho_arr_C%d_dhash(%s)" : "tycho_arr_C%d_hash(%s)", ARRC_ID(t), v);   /* composite-element array: generated, order-sensitive */
    return sfmt(g_hash_det ? "tycho_ik_hash_det((tycho_int)(%s))" : "tycho_ik_hash((tycho_int)(%s))", v);   /* int / bool / char */
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

/* C expression that renders a value `v` of type `t` to a length-headered Tycho
 * string (arena `ar`), for str()/println of an aggregate (F5). Scalars go
 * straight to the runtime to_str helpers; strings are identity (str(s) == s, so
 * a nested string prints raw, no quotes). Aggregates dispatch to a generated
 * tycho_str_* helper (struct/enum/composite array+map) or a fixed runtime one
 * (built-in [int]/[float]/[string] arrays and si/sf/ii/if maps). option/result/
 * tuple are built inline via tycho_str_concat, mirroring gen_eq's structure, so
 * arbitrary nesting works. Format: Point(1, 2) · [1, 2, 3] · [a: 1] · Some(3). */
static char *gen_str(Type t, const char *ar, const char *v) {
    if (IS_NEWTYPE(t))       return gen_str(nt_under(t), ar, v);
    if (t == T_STRING || t == T_BYTES) return sfmt("%s", v);   /* identity: str(s) == s */
    if (t == T_CHAR)         return sfmt("tycho_chr(%s, %s)", ar, v);
    if (t == T_BOOL)         return sfmt("tycho_bool_to_str(%s, %s)", ar, v);
    if (t == T_FLOAT || t == T_F32) return sfmt("tycho_float_to_str(%s, %s)", ar, v);
    if (is_uint(t))          return sfmt("tycho_uint_to_str(%s, %s)", ar, v);
    if (t == T_INT || is_sized_int(t)) return sfmt("tycho_int_to_str(%s, %s)", ar, v);
    if (t == T_ARRAY_INT)    return sfmt("tycho_arr_int_str(%s, %s)", ar, v);
    if (t == T_ARRAY_FLOAT)  return sfmt("tycho_arr_float_str(%s, %s)", ar, v);
    if (t == T_ARRAY_STRING) return sfmt("tycho_arr_str_str(%s, %s)", ar, v);
    if (IS_ARRC(t))          return sfmt("tycho_str_arr_C%d(%s, %s)", ARRC_ID(t), ar, v);
    if (IS_MAPC(t))          return sfmt("tycho_str_mapc%d(%s, %s)", MAPC_ID(t), ar, v);
    if (is_map(t))           return sfmt("tycho_map_%s_str(%s, %s)", map_fn(t), ar, v);
    if (IS_STRUCT(t))        return sfmt("tycho_str_S_%s(%s, %s)", g_structs[STRUCT_ID(t)].name, ar, v);
    if (IS_ENUM(t))          return sfmt("tycho_str_E_%s(%s, %s)", g_enums[ENUM_ID(t)].name, ar, v);
    if (IS_OPT(t)) {
        Type in = opt_inner(t);
        return sfmt("((%s).has ? tycho_str_concat(%s, tycho_str_concat(%s, tycho_str_from_c(%s, \"Some(\"), %s), tycho_str_from_c(%s, \")\")) : tycho_str_from_c(%s, \"None\"))",
                    v, ar, ar, ar, gen_str(in, ar, sfmt("(%s).val", v)), ar, ar);
    }
    if (IS_RES(t)) {
        return sfmt("((%s).ok ? tycho_str_concat(%s, tycho_str_concat(%s, tycho_str_from_c(%s, \"Ok(\"), %s), tycho_str_from_c(%s, \")\")) : tycho_str_concat(%s, tycho_str_concat(%s, tycho_str_from_c(%s, \"Err(\"), %s), tycho_str_from_c(%s, \")\")))",
                    v, ar, ar, ar, gen_str(res_ok(t), ar, sfmt("(%s).okv", v)), ar,
                    ar, ar, ar, gen_str(res_err(t), ar, sfmt("(%s).errv", v)), ar);
    }
    if (IS_TUP(t)) {         /* (e0, e1, ...) folded through tycho_str_concat */
        char *s = sfmt("tycho_str_from_c(%s, \"(\")", ar);
        for (int i = 0; i < tup_n(t); i++) {
            if (i) s = sfmt("tycho_str_concat(%s, %s, tycho_str_from_c(%s, \", \"))", ar, s, ar);
            s = sfmt("tycho_str_concat(%s, %s, %s)", ar, s, gen_str(tup_elem(t, i), ar, sfmt("(%s)._%d", v, i)));
        }
        return sfmt("tycho_str_concat(%s, %s, tycho_str_from_c(%s, \")\"))", ar, s, ar);
    }
    /* fn/soa/handle/ptr as a nested field: no faithful rendering — print the type
     * name honestly (the top-level str() of such a value is rejected at resolve). */
    return sfmt("tycho_str_from_c(%s, \"<%s>\")", ar, type_name(t));
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
 * extra paren, never malformed C.
 *
 * Every input shape gen_expr can hand this, and the verdict for each:
 *   `(h_a < 3LL)`                     STRIP -> `h_a < 3LL`            ok
 *   `({ HTask *_tk = h_t; ...; _w; })` KEEP -- GCC statement-expression: the
 *        '(' is part of the `({ ... })` syntax, NOT a redundant layer.
 *        Stripping it emitted `if ({ ... }) {`, which cc rejects with
 *        "expected expression before '{' token". Reached by `if wait(t):`
 *        over a bool-returning task. This is the s[1] == '{' guard below.
 *   `((h_a && h_b))`                  STRIP -> `(h_a && h_b)`         ok
 *   `h_f(h_x)`                        KEEP  -- does not lead with '('
 *   `(h_a) && (h_b)`                  KEEP  -- two groups; the ')' at index 2
 *        closes before the end, so the p[1] != '\0' test below refuses. (The
 *        classic off-by-one for this kind of scanner; verified absent.)
 *   `((tycho_int)h_x)`                STRIP -> `(tycho_int)h_x`       ok
 *   NULL / `(h_a`                     KEEP  -- empty / unbalanced
 *   `(tycho_streq(h_s, ")") == 1LL)`  STRIP -- literal skip keeps depth honest
 *   `(({ ... }) == 1LL)`              STRIP -> `({ ... }) == 1LL`     ok
 *        (a statement-expression that is not the WHOLE group is safe to expose)
 *   `(tycho_streq(h_s, "abc)`         KEEP  -- unterminated literal
 *   `()`                              KEEP  -- degenerate; stripping emitted
 *        `if () {`. Not reachable from gen_expr today, refused anyway. */
static char *cond_unwrap(char *s) {
    /* s[1] && s[2] rejects "", "(" and "()" -- nothing strippable inside. */
    if (!s || s[0] != '(' || !s[1] || !s[2]) return s;
    if (s[1] == '{') return s;                /* `({ ... })` statement-expression */
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
        Type at = e->args[i]->type;
        const char *arrp = ffi_arr_ptr_ctype(at);
        if (at == T_BYTES) {
            char *tv = sfmt("_xb%d", nb++);
            decls = sfmt("%schar *%s = %s; ", decls, tv, a);
            args = sfmt("%s%s(const unsigned char *)%s, tycho_str_len(%s)", args, emitted++ ? ", " : "", tv, tv);
        } else if (arrp) {   /* [int]/[float] -> (const T*)xs.data, xs.len (single-eval temp) */
            char *tv = sfmt("_xa%d", nb++);
            decls = sfmt("%s%s%s = %s; ", decls, c_type(at), tv, a);
            args = sfmt("%s%s(%s)%s.data, %s.len", args, emitted++ ? ", " : "", arrp, tv, tv);
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
        g_call_arg_depth++;
        for (int i = 0; i < e->nargs; i++)
            out = sfmt("%s, %s", out, gen_expr(e->args[i], g_cur_scope));
        g_call_arg_depth--;
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
                       alias_arg(var->payload[i], arena, e->args[i], e->args, e->nargs));
        return sfmt("%s _p; })", out);
    }
    if (!strcmp(e->sval, "len")) {
        if (is_extern_str_call(e->args[0]))   /* FFI: read the length of a C-owned string without copying it (read-once borrow) */
            return sfmt("({ const char *_x = %s; _x ? (tycho_int)strlen(_x) : 0L; })", gen_extern_raw(e->args[0]));
        char *a = gen_expr(e->args[0], arena);
        if (e->args[0]->type == T_STRING || e->args[0]->type == T_BYTES)   /* bytes: same length-headered buffer */
            return sfmt("tycho_str_len(%s)", a);
        if (IS_FIXARR(e->args[0]->type))   /* [N]T: length is the compile-time N (no .len field) */
            return sfmt("((void)(%s), %lldLL)", a, (long long)fixarr_size(e->args[0]->type));
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
    if (!strcmp(e->sval, "char_at")) {
        /* Deliberately the SAME runtime call `s[i]` emits (E_INDEX, T_STRING
         * above): identical O(1) read, identical bounds abort and message. Only
         * the STATIC type differs -- char_at is T_CHAR, s[i] stays T_INT -- and
         * `char` is carried as tycho_int in C, so the C text is interchangeable. */
        char *s  = gen_expr(e->args[0], arena);
        char *ix = gen_expr(e->args[1], arena);
        return sfmt("tycho_str_get(%s, %s)", s, ix);
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
         * if the root is a heap inout param (so the new buffer outlives the
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
        if (is_map(e->args[0]->type)) {           /* maps: pre-size the entry + index arrays (bench/lru growth waste) */
            Type mt = e->args[0]->type;
            if (IS_MAPC(mt))
                return sfmt("tycho_mapc%d_reserve(%s, &(%s), %s)", MAPC_ID(mt), owner, arr, n);
            return sfmt("tycho_map_%s_reserve(%s, &(%s), %s)", map_fn(mt), owner, arr, n);
        }
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
    if (!strcmp(e->sval, "ncpu"))  return "((tycho_int)tycho_ncpu())";   /* parallel-for fan-out width */
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
    if (!strcmp(e->sval, "chr") || !strcmp(e->sval, "to_char")) {   /* int -> byte. Both route through tycho_chr so both inherit its 0..255 ABORT (`runtime/tycho_rt.c:1184`) rather than masking -- the established answer for an out-of-range conversion here, the same one to_int(float) takes at `runtime/tycho_rt.c:185-187`. to_char then reads the byte back out; the sized to_u8 family wraps instead, but those are documented as total reinterpretations (docs/spec/06-conversions.md:40), not conversions with a domain. */
        return sfmt(e->sval[0] == 'c' ? "tycho_chr(%s, %s)" : "((tycho_int)(unsigned char)tycho_chr(%s, %s)[0])", arena, gen_expr(e->args[0], arena));
    }
    if (!strcmp(e->sval, "die")) {   /* print to stderr and exit(1); never returns */
        return sfmt("tycho_die(%s)", gen_expr(e->args[0], arena));
    }
    /* exit(code): C's exit(3) directly -- no runtime wrapper, because there is
     * nothing to wrap. stdio is flushed by exit() itself, so a `println` before it
     * is not lost, and only the low 8 bits reach the parent (POSIX wait status).
     * Never returns; see expr_diverges. */
    if (!strcmp(e->sval, "exit")) {
        return sfmt("exit((int)(%s))", gen_expr(e->args[0], arena));
    }
    if (!strcmp(e->sval, "str")) {
        Type at = e->args[0]->type;
        char *a = gen_expr(e->args[0], arena);
        Type b = base_of(at);
        if (b == T_STRING || b == T_BYTES) return a;  /* str(string) is identity (interpolation); a string newtype/bytes print their bytes raw */
        if (b == T_CHAR)   return sfmt("tycho_chr(%s, %s)", arena, a);   /* char -> its one-byte glyph string (value is a byte 0..255) */
        if (b == T_BOOL)   return sfmt("tycho_bool_to_str(%s, %s)", arena, a);
        if (b == T_FLOAT || b == T_F32) return sfmt("tycho_float_to_str(%s, %s)", arena, a);   /* f32 promotes to double */
        if (is_uint(b))   return sfmt("tycho_uint_to_str(%s, %s)", arena, a);   /* u8/u16/u32/u64 print unsigned */
        if (b == T_INT || is_sized_int(b)) return sfmt("tycho_int_to_str(%s, %s)", arena, a);   /* int + signed sized (i8/i16/i32/i64) */
        /* F5 aggregate: bind the value ONCE (opt/result/tuple renderers read it repeatedly), then recurse via gen_str */
        return sfmt("({ %s_sv = %s; %s; })", c_type(at), a, gen_str(at, arena, "_sv"));
    }
    if (!strcmp(e->sval, "hash")) {   /* generic hash (see resolve): DETERMINISTIC (cross-run stable), 64-bit as signed int */
        char *a = gen_expr(e->args[0], arena);
        g_hash_det = 1;
        char *h = gen_hash(e->args[0]->type, a);
        g_hash_det = 0;
        return sfmt("((tycho_int)(%s))", h);
    }
    if (!strcmp(e->sval, "to_float"))    /* int -> double */
        return sfmt("((double)%s)", gen_expr(e->args[0], arena));
    if (!strcmp(e->sval, "to_int")) {    /* float -> long via the range/NaN guard (traps out-of-range); sized/u32/u64 -> long is defined, plain cast */
        Type at = base_of(e->args[0]->type);
        if (at == T_FLOAT || at == T_F32)
            return sfmt("tycho_f2i(%s)", gen_expr(e->args[0], arena));
        return sfmt("((tycho_int)%s)", gen_expr(e->args[0], arena));
    }
    if (!strcmp(e->sval, "to_ptr"))      /* int -> void* (FFI sentinel pointer; tycho never derefs it) */
        return sfmt("((void*)(tycho_int)%s)", gen_expr(e->args[0], arena));
    if (is_sized_conv(e->sval))          /* to_u8..to_i64, to_f32: cast to the target C type (truncate / sign- or zero-extend) */
        return sfmt("((%s)%s)", c_type(sized_conv_target(e->sval)), gen_expr(e->args[0], arena));
    if (!strcmp(e->sval, "to_bytes") && base_of(e->args[0]->type) == T_ARRAY_INT)   /* [int] -> bytes: real conversion (each elem & 0xFF into a fresh binary buffer) */
        return sfmt("tycho_bytes_from_intarr(%s, %s)", arena, gen_expr(e->args[0], arena));
    if (!strcmp(e->sval, "to_str") || !strcmp(e->sval, "to_bool") || !strcmp(e->sval, "to_under") || !strcmp(e->sval, "to_bytes"))   /* zero-cost newtype unwrap / bytes<->string reinterpret (identical char* repr) */
        return gen_expr(e->args[0], arena);
    if (!strcmp(e->sval, "sqrt") || !strcmp(e->sval, "floor") || !strcmp(e->sval, "fabs"))   /* libm, 1 float arg */
        return sfmt("%s(%s)", e->sval, gen_expr(e->args[0], arena));
    if (!strcmp(e->sval, "pow"))   /* libm, 2 float args */
        return sfmt("pow(%s, %s)", gen_expr(e->args[0], arena), gen_expr(e->args[1], arena));
    /* user proc: first arg is the destination arena for its return. A heap
     * inout parameter takes TWO C args: the value's owning arena, then the
     * &pointer — so an allocating mutation in the callee lands where the
     * value lives. The owner is computed from the argument's root variable
     * (which, if it's itself a heap inout param here, yields its carried
     * _ina_ arena — threading the real owner across recursion). */
    Sig *cs = sig_find(e->sval);
    if (cs && cs->is_extern) {
        /* FFI: call the C symbol directly — no arena, no h_ prefix. Args are
         * built in the current scope (tycho str is already a char*, so a string
         * arg passes zero-cost). A string RETURN is C-owned, so copy it into the
         * destination arena (NULL -> "") so tycho never holds a foreign pointer. */
        Type _retb = base_of(cs->ret);
        if (_retb == T_BYTES || _retb == T_ARRAY_INT || _retb == T_ARRAY_FLOAT) {
            /* out-param shim: synthesize (T** out, long* outlen), call, then copy the
             * C-owned buffer into an arena bytes/array and free it. The shim must
             * malloc(*out); tycho_{bytes,arr_int,arr_float}_from_c frees it after. */
            char *decls = sfmt("%s", ""), *args = sfmt("%s", "");
            int emitted = 0, nb = 0;
            for (int i = 0; i < e->nargs; i++) {
                char *a = gen_expr(e->args[i], g_cur_scope);
                Type at = e->args[i]->type;
                const char *arrp = ffi_arr_ptr_ctype(at);
                if (at == T_BYTES) {
                    char *tv = sfmt("_xb%d", nb++);
                    decls = sfmt("%schar *%s = %s; ", decls, tv, a);
                    args = sfmt("%s%s(const unsigned char *)%s, tycho_str_len(%s)", args, emitted++ ? ", " : "", tv, tv);
                } else if (arrp) {   /* [int]/[float] -> (const T*)xs.data, xs.len */
                    char *tv = sfmt("_xa%d", nb++);
                    decls = sfmt("%s%s%s = %s; ", decls, c_type(at), tv, a);
                    args = sfmt("%s%s(%s)%s.data, %s.len", args, emitted++ ? ", " : "", arrp, tv, tv);
                } else {
                    args = sfmt("%s%s%s", args, emitted++ ? ", " : "", a);
                }
            }
            int id = g_blk++;
            const char *octype = _retb == T_BYTES ? "unsigned char" : _retb == T_ARRAY_INT ? "tycho_int" : "double";
            const char *fromc  = _retb == T_BYTES ? "tycho_bytes_from_c" : _retb == T_ARRAY_INT ? "tycho_arr_int_from_c" : "tycho_arr_float_from_c";
            return sfmt("({ %s%s *_o%d = 0; tycho_int _ol%d = 0; %s(%s%s&_o%d, &_ol%d); %s(%s, _o%d, _ol%d); })",
                        decls, octype, id, id, e->sval, args, emitted ? ", " : "", id, id, fromc, arena, id, id);
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
    g_call_arg_depth++;
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
    g_call_arg_depth--;
    return sfmt("%s)", out);
}

/* F: cast an operation's result back to its sized-int C type. Sub-int types
 * (u8/u16/i8/i16) promote to `int` in C arithmetic, so a sub-expression would
 * otherwise hold the un-truncated promoted value; the wider sized types take a
 * harmless redundant cast. Non-sized types pass through unchanged. */
static char *trunc_result(Type t, char *expr) {
    Type b = base_of(t);
    if (b == T_CHAR) return sfmt("((tycho_int)(unsigned char)(%s))", expr);   /* char arithmetic stays a byte 0..255 (char is one byte, like u8) */
    return is_sized_int(b) ? sfmt("(%s)(%s)", c_type(b), expr) : expr;
}

/* Emit one signed integer literal as C source of rank >= `long long`.
 *
 * `LL` (C long long, >= 64-bit on EVERY data model), not `L`: a plain `L` is
 * 32-bit under ILP32/LLP64, so `100000L * 100000L` truncates in the multiply --
 * before the store into the 64-bit destination.
 *
 * THE MINIMUM IS SPECIAL. C has no negative integer constants: `-9223372036854775808LL`
 * is unary `-` applied to the constant `9223372036854775808LL`, and that constant's
 * type is chosen BEFORE the negation. 2^63 does not fit `long long`, so C99/C11
 * 6.4.4.1p5 has no type for it -- GCC/clang accept it as an extension with
 * "warning: integer constant is so large that it is unsigned" (default-on, fires
 * on every user build; tychoc's own cc line at the bottom of this file passes no
 * -Wall, so this was the only thing announcing it). The value came out right on
 * two's-complement hosts, but the emitter was relying on a construct the standard
 * does not grant it. `(-9223372036854775807LL - 1)` is the conventional spelling
 * (it is how <stdint.h> itself defines INT64_MIN): every constituent constant fits
 * `long long`, the arithmetic is exact and non-overflowing, and the result type is
 * still `long long`. Parenthesized so it is safe in any operand position.
 *
 * ONLY the 64-bit minimum needs this. The narrower sized minima (i8 -128, i16
 * -32768, i32 -2147483648) are emitted through this same path, and their magnitudes
 * (128 / 32768 / 2147483648) all fit `long long`, so the negation is well-typed --
 * verified by emitting the whole fixture suite and finding zero such warnings once
 * the 64-bit case is handled. u32/u64 never reach here: they take the `%lldU` /
 * `%lldULL` arms above, where an unsigned suffix makes even a top-bit-set value a
 * legal constant. Newtypes over `int` and `char` reach here through `base_of`, and
 * are covered by the same test because they carry their value in the same int64_t. */
static char *c_int_lit(int64_t v) {
    if (v == INT64_MIN) return sfmt("(-9223372036854775807LL - 1)");
    return sfmt("%lldLL", (long long)v);
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

/* Emit one SCALAR arithmetic/bitwise/shift operation of result type `rt` over
 * already-emitted operands `l` and `r`. Factored out of E_BINOP so that the
 * element-wise array arm can reuse it verbatim for `a[i] OP b[i]`: the guards
 * below (int `/` and `%` through tycho_idiv/imod, unsigned through udiv/umod,
 * shifts through the width guard, sized results truncated to their C width) are
 * exactly the ones a scalar gets, and an element must not silently get fewer. */
static char *gen_arith_op(TokKind op, Type rt, char *l, char *r) {
    /* integer division/modulo guard: -fwrapv (the int-overflow contract) makes
     * signed overflow wrap, but division is the one arithmetic op it does NOT
     * make total -- x/0, x%0, and LONG_MIN/-1 still trap (SIGFPE). Route int `/`
     * and `%` (incl. an int newtype) through the runtime guard, which aborts
     * cleanly with a tycho: message. Signed native/sized -> tycho_idiv/imod;
     * unsigned sized -> udiv/umod. Float `/` is IEEE (x/0.0 = inf), so it stays
     * a raw C operator. */
    if (op == TK_SLASH || op == TK_PERCENT) {
        Type b = base_of(rt);
        if (b == T_INT)
            return sfmt("%s(%s, %s)", op == TK_SLASH ? "tycho_idiv" : "tycho_imod", l, r);
        if (is_uint(b))
            return trunc_result(rt, sfmt("%s(%s, %s)", op == TK_SLASH ? "tycho_udiv" : "tycho_umod", l, r));
        if (is_sized_int(b))   /* signed sized: i8/i16/i32/i64 */
            return trunc_result(rt, sfmt("%s(%s, %s)", op == TK_SLASH ? "tycho_idiv" : "tycho_imod", l, r));
        /* float / falls through to the raw operator below */
    }
    /* shifts route through the runtime guard: count >= width -> 0, negative
     * count -> clean abort (both C UB otherwise). int/u32/u64 have dedicated
     * helpers; the narrow and i32/i64 types use the generic width+sign guard,
     * then truncate the result to their C width. */
    if (op == TK_SHL || op == TK_SHR) {
        Type b = base_of(rt);
        char c = op == TK_SHL ? 'l' : 'r';
        if (b == T_INT) return sfmt("tycho_sh%c_i(%s, %s)", c, l, r);
        if (b == T_U32) return sfmt("tycho_sh%c_u32(%s, %s)", c, l, r);
        if (b == T_U64) return sfmt("tycho_sh%c_u64(%s, %s)", c, l, r);
        char *g = op == TK_SHL
            ? sfmt("tycho_shln(%s, %s, %d)", l, r, int_width(b))
            : sfmt("tycho_shrn(%s, %s, %d, %d)", l, r, int_width(b), !is_uint(b));
        return trunc_result(rt, g);
    }
    return trunc_result(rt, sfmt("(%s %s %s)", l, op_str(op), r));
}

/* Emit element-wise `a OP b` -- two arrays, or one array and one broadcast
 * scalar -- as one GCC statement-expression that builds a FRESH array of type
 * `e->type` and returns it. Freshness is
 * the point: Tycho is value-semantics, so `c := a * b` must neither alias nor
 * mutate `a` or `b`. Both operands are copied into locals first (so a
 * side-effecting operand is evaluated exactly once), the result gets its own
 * spine out of the arena, and every element is written by value. The element
 * types this arm can reach are all scalars (elem_arith_ok admits int, the sized
 * ints, float, f32, char and numeric newtypes only), so a shallow element store
 * IS a deep copy and no copy_into is needed.
 *
 * Two shapes, because the two array kinds have different C structs:
 *   [T]   -> { data, len, cap }: lengths must match, checked at RUNTIME.
 *   [N]T  -> { v[N] }:           lengths match by construction, the typechecker
 *                                already refused two different static Ns.
 * A broadcast has one array and one scalar, so it has no second length and
 * emits no runtime check at all. */
static char *gen_ew_arith(Expr *e, char *l, char *r, const char *arena) {
    Type at = e->type, et = arr_elem(at);
    const char *act = c_type(at), *ect = c_type(et);
    /* Which sides are arrays. Exactly one may be a scalar (broadcast); the
     * typechecker has already refused every other combination. `_ewa` is ALWAYS
     * the lhs and `_ewb` ALWAYS the rhs -- the operands are never reordered, so
     * `2 - a` emits `2 - a[i]` and `a - 2` emits `a[i] - 2`. */
    int la = is_array(e->lhs->type), ra = is_array(e->rhs->type);
    const char *ax = la ? (IS_FIXARR(at) ? "_ewa.v[_ewi]" : "_ewa.data[_ewi]") : "_ewa";
    const char *bx = ra ? (IS_FIXARR(at) ? "_ewb.v[_ewi]" : "_ewb.data[_ewi]") : "_ewb";
    char *body = gen_arith_op(e->op, et, (char *)ax, (char *)bx);
    /* each local takes its own C type: the array side the array struct, the
     * scalar side the element type (so it is evaluated exactly once, like the
     * array operands, and a side-effecting scalar is not re-run per element). */
    const char *lct = la ? act : ect, *rct = ra ? act : ect;
    const char *src = la ? "_ewa" : "_ewb";   /* the array side: length + shape */
    if (IS_FIXARR(at))
        return sfmt("({ %s_ewa = (%s); %s_ewb = (%s); %s_ewr = %s;\n"
                    "   for (tycho_int _ewi = 0; _ewi < %lld; _ewi++) _ewr.v[_ewi] = %s;\n"
                    "   _ewr; })",
                    lct, l, rct, r, act, src, (long long)fixarr_size(at), body);
    /* a broadcast has only one length, so there is nothing to mismatch: the
     * runtime check is emitted only when BOTH sides are arrays. */
    return sfmt("({ %s_ewa = (%s); %s_ewb = (%s);\n"
                "%s"
                "   %s_ewr; _ewr.len = %s.len; _ewr.cap = %s.len;\n"
                "   _ewr.data = _ewr.len ? (%s*)arena_alloc(%s, (size_t)_ewr.len * sizeof(%s)) : 0;\n"
                "   for (tycho_int _ewi = 0; _ewi < _ewr.len; _ewi++) _ewr.data[_ewi] = %s;\n"
                "   _ewr; })",
                lct, l, rct, r,
                (la && ra) ? "   if (_ewa.len != _ewb.len) tycho_ew_len(_ewa.len, _ewb.len);\n" : "",
                act, src, src, ect, arena, ect, body);
}

static char *gen_expr(Expr *e, const char *arena) {
    switch (e->kind) {
        case E_SPREAD:   /* unreachable: resolve rewrites/rejects every spread before codegen */
            die_at(e->line, "internal: spread `...` reached codegen");
        case E_INT:
            /* u32/u64 literals need an unsigned C suffix: an unadorned `...L` is signed
             * long, so `u32_var + 3000000000L` would promote to signed 64-bit and wrap at
             * 2^64, not 2^32. `U`/`ULL` keep the arithmetic at the value's width. */
            if (e->type == T_U32) return sfmt("%lldU", (long long)e->ival);
            if (e->type == T_U64) return sfmt("%lldULL", (long long)e->ival);
            /* signed: `LL` rank, with the 2^63 minimum spelled so it stays a legal
             * C constant expression -- see c_int_lit(). */
            return c_int_lit(e->ival);
        case E_CHAR: return c_int_lit(e->ival);  /* a byte value carried as tycho_int */
        case E_FLOAT: {
            /* %.17g round-trips the double exactly; ensure the C literal reads
             * as a double (has '.', 'e', or is inf/nan) so e.g. 3.0 / 2.0 is
             * not integer division. c_dtoa, not a bare snprintf: the separator
             * must be '.' before the scan below can mean anything -- under a
             * comma locale every finite value looks integral to it and the guard
             * turns `1,5` into `1,5.0`, a comma expression cc accepts. The
             * conversion and the guard are one change; see c_numeric_handle.
             * NON-FINITE CANNOT REACH HERE, and the 'n'/'i' arm of the scan is
             * kept as the guard's stated contract rather than as a live path.
             * Every fval is finite by construction: the lexer now REFUSES an
             * out-of-range literal (see the DBL_MAX check in lex_num), int->float
             * literal conversion and the synthesized 0.0/1.0 seeds start finite,
             * and const_fold folds only unary minus on a float -- its binop arm
             * bails on any non-E_INT operand, so 1.0/0.0 is never folded. WIDEN
             * const_fold TO FLOAT ARITHMETIC AND `inf` COMES BACK HERE as the
             * bare token cc rejects; give it a portable form (HUGE_VAL, or a
             * 1.0/0.0 construction) in the same change. */
            char b[64];
            c_dtoa(b, sizeof b, e->fval);
            int isfloaty = 0;
            for (char *q = b; *q; q++)
                if (*q == '.' || *q == 'e' || *q == 'E' || *q == 'n' || *q == 'i') { isfloaty = 1; break; }
            return isfloaty ? sfmt("%s", b) : sfmt("%s.0", b);
        }
        case E_BOOL: return sfmt("%lld", (long long)e->ival);
        case E_NULL: return sfmt("((void*)0)");
        case E_STR:  /* a length-headered, interned-once copy (cached per occurrence) */
            return sfmt("({ static char *_l = 0; if (!_l) _l = tycho_str_intern(\"%s\"); _l; })", e->sval);
        case E_NONE: return sfmt("(%s){0}", c_type(e->type));   /* has = 0 */
        case E_SOME: {
            Type inner = opt_inner(e->type);   /* move/copy the value into the option */
            return sfmt("(%s){ 1, %s }", c_type(e->type), alias_arg(inner, arena, e->lhs, NULL, 0));
        }
        case E_OK:    /* designated init: errv auto-zeroes, dodging -Wmissing-field-initializers */
            return sfmt("(%s){ .ok = 1, .okv = %s }", c_type(e->type),
                        alias_arg(res_ok(e->type), arena, e->lhs, NULL, 0));
        case E_ERR:
            return sfmt("(%s){ .ok = 0, .errv = %s }", c_type(e->type),
                        alias_arg(res_err(e->type), arena, e->lhs, NULL, 0));
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
                char *a = alias_arg(tup_elem(e->type, i), arena, e->args[i], e->args, e->nargs);
                out = sfmt("%s%s%s", out, a, i + 1 < e->nargs ? ", " : "");
            }
            return sfmt("%s }", out);
        }
        case E_TUPIDX:   /* t.0 -> (t)._0 */
            return sfmt("((%s)._%lld)", gen_expr(e->lhs, arena), (long long)e->ival);
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
        case E_ADDR: /* &place as an inout arg: address of the underlying
                      * lvalue. gen_lvalue derefs an inout root and projects an
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
                char *out = sfmt("({ Soa%d *_g%d = &(%s); tycho_int _gi%d = Soa%d_bound(_g%d, %s); (S_%s){ ",
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
            if (e->lhs->type == T_STRING || e->lhs->type == T_BYTES)
                return sfmt("tycho_str_get(%s, %s)", a, ix);   /* O(1): length header, no strlen (bytes: same buffer) */
            if (index_in_range(e->lhs, e->rhs))               /* monotone loop index: skip the bounds check */
                return sfmt("(%s).data[%s]", a, ix);
            return sfmt("tycho_arr_%s_get(%s, %s)", arr_fn(e->lhs->type), a, ix);
        }
        case E_SLICE: {
            if (e->lhs->type == T_STRING || e->lhs->type == T_BYTES) {   /* s[a:b] / b[a:b] -> a fresh sub-buffer (substr; byte-safe, header lengths) */
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
                return sfmt("({ Soa%d _sv%d = %s; tycho_int _lo%d = %s, _hi%d = %s; "
                            "if (_lo%d < 0 || _hi%d > _sv%d.len || _lo%d > _hi%d) { "
                            "fprintf(stderr, \"tycho: slice [%%\" TY_PRId \":%%\" TY_PRId \"] out of bounds (len %%\" TY_PRId \")\\n\", _lo%d, _hi%d, _sv%d.len); exit(1); } "
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
            return sfmt("({ %s_sv%d = %s; tycho_int _lo%d = %s, _hi%d = %s; "
                        "if (_lo%d < 0 || _hi%d > _sv%d.len || _lo%d > _hi%d) { "
                        "fprintf(stderr, \"tycho: slice [%%\" TY_PRId \":%%\" TY_PRId \"] out of bounds (len %%\" TY_PRId \")\\n\", _lo%d, _hi%d, _sv%d.len); exit(1); } "
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
            if (IS_FIXARR(e->type)) {   /* [N]T fixed literal: fill the inline v[] (elements deep-copied into `arena`) */
                Type felem = arr_elem(e->type);
                char *out = sfmt("({ %s_l%d;", c_type(e->type), id);
                for (int i = 0; i < e->nargs; i++)
                    out = sfmt("%s _l%d.v[%d] = %s;", out, id, i, alias_arg(felem, arena, e->args[i], e->args, e->nargs));
                return sfmt("%s _l%d; })", out, id);
            }
            if (IS_BOUNDED(e->type)) {   /* bounded[N]T literal: fill v[0..k], len = k (k <= N, checked in resolve_exp) */
                Type felem = arr_elem(e->type);
                char *out = sfmt("({ %s_l%d = {0};", c_type(e->type), id);
                for (int i = 0; i < e->nargs; i++)
                    out = sfmt("%s _l%d.v[%d] = %s;", out, id, i, alias_arg(felem, arena, e->args[i], e->args, e->nargs));
                return sfmt("%s _l%d.len = %dL; _l%d; })", out, id, e->nargs, id);
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
                           alias_arg(elem, arena, e->args[i], e->args, e->nargs));
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
                char *a = alias_arg(sd->fields[i].type, arena, e->args[i], e->args, e->nargs);
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
            if (e->op == TK_TILDE)                     /* unary bitwise NOT (truncated to width for a sized int) */
                return trunc_result(e->type, sfmt("(~%s)", gen_expr(e->lhs, arena)));
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
            if (e->op == TK_PLUS && (e->lhs->type == T_STRING || e->lhs->type == T_BYTES))
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
            /* element-wise arithmetic (two arrays, or an array and a broadcast
             * scalar): the only binary operator whose result type is an array
             * (`==` yields bool, `+` on string / bytes yields those), so the
             * result type alone identifies it. */
            if (is_array(e->type))
                return gen_ew_arith(e, l, r, arena);
            /* every scalar arithmetic/bitwise/shift op, incl. the division and
             * shift runtime guards -- shared with the element-wise arm above. */
            return gen_arith_op(e->op, e->type, l, r);
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
/* Per enclosing loop: the block id of its post clause, or -1 if it has none.
 * A `continue` in a three-clause loop MUST run the post clause, so it is emitted
 * as `goto _post<id>` rather than a C `continue` (which jumps straight to the
 * condition and would skip it -- a loop that only advances in its post clause
 * would spin forever). Every loop writes its slot before g_loop_depth++, so a
 * plain loop's -1 always overwrites whatever a sibling three-clause loop left. */
static int g_loop_post[64];

/* Does a `continue` in this body bind to THIS loop? Descends through if / match /
 * select bodies (a continue there escapes outward to the enclosing loop) but not
 * into a nested loop, which captures its own. Used only to decide whether the
 * post-clause label is emitted at all, so no unused label ever reaches cc. */
static int body_continues(Stmt **body, int n) {
    for (int i = 0; i < n; i++) {
        Stmt *s = body[i];
        if (!s) continue;
        if (s->kind == S_CONTINUE) return 1;
        if (s->kind == S_WHILE || s->kind == S_FORRANGE || s->kind == S_FOR3) continue;
        if (body_continues(s->body, s->nbody)) return 1;
        if (body_continues(s->els, s->nels)) return 1;
        for (int a = 0; a < s->narms; a++)
            if (body_continues(s->arms[a].body, s->arms[a].nbody)) return 1;
        if (s->ctrl && body_continues(&s->ctrl, 1)) return 1;
    }
    return 0;
}
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
 * `&arr[i].x` (inout) all mutate the element in place — Hylo-style projection,
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
        return sfmt("((%s)._%lld)", gen_lvalue(e->lhs, arena), (long long)e->ival);
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
        /* element-pointer projection: arr_fn dispatches to the built-in scalar
         * families (tycho_arr_int/str/float_ptr) or a composite tycho_arr_C<id>_ptr.
         * The scalar ones previously did not exist, so a scalar-element inout
         * (`inc(&a[i])`) emitted a bogus tycho_arr_C<garbage>_ptr and failed to build. */
        return sfmt("(*tycho_arr_%s_ptr(&(%s), %s))",
                    arr_fn(e->lhs->type), gen_lvalue(e->lhs, arena),
                    gen_expr(e->rhs, arena));
    }
    return gen_expr(e, arena);
}

/* Emit ONE side of an Option/Result match -- its Ok, Err or Some arms -- as an
 * ordered decision chain. `val` is the C expression for the payload (`_m3.errv`),
 * `pt` its Tycho type.
 *
 * Refined arms (a nested pattern, arm->sub_vi >= 0) become tag tests on the payload
 * enum, in SOURCE order. The unrefined arm -- or the `_` wildcard -- is the trailing
 * else. With neither, the else traps, exactly as the enum dispatch in gen_stmt does:
 * the resolver has already proved the side total, so this is unreachable, but it
 * keeps the generated C provably returning on every path.
 *
 * A refined arm binds nothing from the payload itself (the test IS the information);
 * it binds only its nested variant's own payload fields, borrowed or copied on the
 * same rule the enum dispatch uses. */
static void gen_match_side(FILE *o, Stmt *s, const char *tag, Type pt, char *val,
                           int ind, MatchArm *wildarm, const char *scope, Type ret) {
    MatchArm *plain = NULL;
    int nref = 0;
    for (int i = 0; i < s->narms; i++) {
        MatchArm *a = &s->arms[i];
        if (strcmp(a->variant, tag)) continue;
        if (a->sub_vi < 0) { plain = a; continue; }
        EnumDef *ed = &g_enums[ENUM_ID(pt)];
        Variant *var = &ed->variants[a->sub_vi];
        indent(o, ind);
        fprintf(o, "%sif (%s->tag == %d) {\n", nref ? "else " : "", val, a->sub_vi);
        nref++;
        int m = cv_mark();
        if (var->npayload > 0 && a->nsubbinds > 0) {
            int bid = g_blk++;
            indent(o, ind + 1);
            fprintf(o, "E_%s_%s *_p%d = &%s->u.%s;\n", ed->name, var->name, bid, val, var->name);
            for (int b = 0; b < a->nsubbinds; b++) {
                char *field = sfmt("_p%d->f%d", bid, b);
                int borrow = type_is_heap(var->payload[b])
                    && !block_mutates(a->body, a->nbody, a->subbinds[b]);
                indent(o, ind + 1);
                fprintf(o, "%sh_%s = %s;\n", c_type(var->payload[b]), a->subbinds[b],
                        borrow ? field : copy_into(var->payload[b], scope, field));
                cv_push(a->subbinds[b], borrow ? NULL : scope);
            }
        }
        gen_block(o, a->body, a->nbody, ind + 1, scope, ret);
        cv_restore(m);
        indent(o, ind); fprintf(o, "}\n");
    }
    int bind = ind + (nref ? 1 : 0);
    if (nref) { indent(o, ind); fprintf(o, "else {\n"); }
    if (plain) {
        int borrow = type_is_heap(pt) && !block_mutates(plain->body, plain->nbody, plain->binds[0]);
        indent(o, bind);
        fprintf(o, "%sh_%s = %s;\n", c_type(pt), plain->binds[0],
                borrow ? val : copy_into(pt, scope, val));
        int m = cv_mark(); cv_push(plain->binds[0], borrow ? NULL : scope);
        gen_block(o, plain->body, plain->nbody, bind, scope, ret);
        cv_restore(m);
    } else if (wildarm) {
        gen_block(o, wildarm->body, wildarm->nbody, bind, scope, ret);
    } else {
        indent(o, bind);
        fprintf(o, "fprintf(stderr, \"tycho: non-exhaustive match\\n\"); exit(1);\n");
    }
    if (nref) { indent(o, ind); fprintf(o, "}\n"); }
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
    indent(o, ind + 1); fprintf(o, "tycho_int _plo = %s, _phi = %s;\n", lo, hi);
    indent(o, ind + 1); fprintf(o, "if (_phi < _plo) _phi = _plo;\n");
    indent(o, ind + 1); fprintf(o, "tycho_int _pk = tycho_ncpu();\n");
    indent(o, ind + 1); fprintf(o, "if (_pk > _phi - _plo) _pk = _phi - _plo;\n");
    indent(o, ind + 1); fprintf(o, "if (_pk < 1) _pk = 1; if (_pk > 64) _pk = 64;\n");
    indent(o, ind + 1); fprintf(o, "HTask *_pts[64];\n");
    indent(o, ind + 1); fprintf(o, "for (tycho_int _pc = 0; _pc < _pk; _pc++) {\n");
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
    indent(o, ind + 1); fprintf(o, "for (tycho_int _pc = 0; _pc < _pk; _pc++) {\n");
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
            if ((s->decl_type == T_STRING || s->decl_type == T_BYTES) && is_accum(s->name)) {
                indent(o, ind);
                fprintf(o, "tycho_int _len_h_%s = ((const tycho_int *)h_%s)[-1]; tycho_int _cap_h_%s = 0;\n",
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
            /* MM-10b: a top-level SCALAR assign with an allocating RHS — the RHS heap
             * is all transient (result is a scalar), and at function top level there
             * is no per-iteration reset to reclaim it, so it would sit in _scope until
             * return. Build it in a per-statement _t arena. Gated to "&_scope" (in a
             * loop/block the scratch reset already reclaims) and excludes or_return
             * (early return would skip arena_free) and inout (LHS is *h_, leave it). */
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
            /* (an inout param is excluded: its _len_/_cap_ trackers are never
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
                 * inout map param the descriptor is the caller's (pointer h_m,
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
             * allocation, so no arena arg; the pointer is the inout pointer for
             * an inout map, else the address of the local descriptor. */
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
                && is_array(s->expr->type) && !IS_BOUNDED(s->expr->type)   /* bounded has no .data spine to recycle */
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
                /* an inout param is a pointer in the body; assign through it */
                if (is_inout_param(s->name))
                    fprintf(o, "(*h_%s) = %s;\n", s->name, v);
                else
                    fprintf(o, "h_%s = %s;\n", s->name, v);
            }
            /* a non-self assignment to a tracked accumulator rebinds its
             * buffer; resync sidecars (cap 0 = the new buffer isn't ours to
             * grow in place — forces the next append to allocate). */
            if (is_accum(s->name) && (s->expr->type == T_STRING || s->expr->type == T_BYTES) && !is_inout_param(s->name))
                fprintf(o, "%*s_len_h_%s = ((const tycho_int *)h_%s)[-1]; _cap_h_%s = 0;\n",
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
                 * a heap inout param. */
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
            /* MM-10: an expression statement's value is DISCARDED, so every transient
             * it allocates is dead at statement end. At function top level (scope is
             * "&_scope") there is no per-iteration reset to reclaim them, so build them
             * in a fresh per-statement `_t` arena (block-scoped, like scalar_transient)
             * and free it immediately, instead of letting them accumulate in the
             * enclosing scope until function return. Sound because stores into
             * longer-lived containers / inout route through owner_arena_of, not
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
            g_in_return++;
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
            } else if (ret == T_BYTES) {
                /* bytes shares string's length-headered char* repr, so it
                 * promotes up exactly like T_STRING: a fresh value (an out-param
                 * extern call) is built directly in _parent; a bare bytes place
                 * is deep-copied there before this scope frees. tycho_str_copy is
                 * length-based (header, not strlen), so interior NULs survive.
                 * Without this branch a returned bytes fell to the scalar `else`
                 * and was built in the freed &_scope -- a use-after-free (the only
                 * `-> bytes`-returning code is core:net; tychoc0.ty has none, so
                 * this is output-invisible to fixpoint + the golden suite). */
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
                 * optimization already built it in _parent (then a no-op move).
                 * The copy goes through copy_into, NOT tycho_map_%s_copy(map_fn):
                 * map_fn defaults to "si" for every map that is not sf/ii/if, so
                 * a composite map ([string: Struct], [string: string]) returned
                 * from a PARAMETER emitted tycho_map_si_copy over a TychoMapC0
                 * and the generated C did not compile. Returning a LOCAL dodged
                 * it via the return-slot no-op move; only the param-copy path
                 * reached the wrong name. Found by the tycho-scheme compiler
                 * (plan phase: current_scope's workaround). */
                if (ret_must_copy(s->expr)) {
                    char *v = gen_expr(s->expr, "&_scope");
                    indent(o, ind); fprintf(o, "{ %s_ret = %s; %s return _ret; }\n",
                                            c_type(ret), copy_into(ret, "_parent", v), rf);
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
            g_in_return--;
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
                if (s->arms[i].variant && !strcmp(s->arms[i].variant, "_")) wildarm = &s->arms[i];
            if (IS_OPT(st)) {
                MatchArm *none = NULL;
                for (int i = 0; i < s->narms; i++)
                    if (!strcmp(s->arms[i].variant, "None")) none = &s->arms[i];
                indent(o, ind + 1); fprintf(o, "if (_m%d.has) {\n", mid);
                gen_match_side(o, s, "Some", opt_inner(st), sfmt("_m%d.val", mid),
                               ind + 2, wildarm, scope, ret);
                indent(o, ind + 1); fprintf(o, "} else {\n");   /* None binds nothing */
                gen_block(o, none ? none->body : wildarm->body, none ? none->nbody : wildarm->nbody, ind + 2, scope, ret);
                indent(o, ind + 1); fprintf(o, "}\n");
            } else if (IS_RES(st)) {   /* Ok(x) -> .okv / Err(e) -> .errv, tag is .ok */
                indent(o, ind + 1); fprintf(o, "if (_m%d.ok) {\n", mid);
                gen_match_side(o, s, "Ok", res_ok(st), sfmt("_m%d.okv", mid),
                               ind + 2, wildarm, scope, ret);
                indent(o, ind + 1); fprintf(o, "} else {\n");
                gen_match_side(o, s, "Err", res_err(st), sfmt("_m%d.errv", mid),
                               ind + 2, wildarm, scope, ret);
                indent(o, ind + 1); fprintf(o, "}\n");
            } else if (st == T_INT || st == T_CHAR || st == T_BOOL) {
                /* Scalar match. With >= 4 arms and no range wider than 64, a
                 * switch-of-gotos: the goto keeps a user `break`/`continue` in
                 * an arm body targeting the surrounding LOOP, not this switch
                 * (S_BREAK emits a plain C `break`), and cc -O3 lowers the
                 * dense switch to a jump table. Below that, a plain if/else
                 * chain. The resolver guarantees `_` for int/char and full
                 * coverage for bool; the default below is the same fail-closed
                 * backstop as the enum else. The resolver has folded const
                 * names to values, so only plo/phi are read here. */
                int wide = 0;
                for (int i = 0; i < s->narms; i++)
                    for (int k = 0; k < s->arms[i].pn; k++)
                        if (s->arms[i].phi[k] - s->arms[i].plo[k] > 64) wide = 1;
                if (s->narms >= 4 && !wide) {
                    int wildidx = -1;
                    for (int i = 0; i < s->narms; i++)
                        if (s->arms[i].variant && !strcmp(s->arms[i].variant, "_")) wildidx = i;
                    indent(o, ind + 1); fprintf(o, "switch (_m%d) {\n", mid);
                    for (int i = 0; i < s->narms; i++) {
                        MatchArm *arm = &s->arms[i];
                        if (arm->variant) continue;      /* the `_` arm is the default */
                        indent(o, ind + 2);
                        for (int k = 0; k < arm->pn; k++)
                            for (int64_t v = arm->plo[k]; ; v++) {
                                fprintf(o, "case %lld: ", (long long)v);
                                if (v == arm->phi[k]) break;
                            }
                        fprintf(o, "goto _m%d_a%d;\n", mid, i);
                    }
                    indent(o, ind + 2);
                    if (wildidx >= 0) fprintf(o, "default: goto _m%d_a%d;\n", mid, wildidx);
                    else fprintf(o, "default: fprintf(stderr, \"tycho: non-exhaustive match\\n\"); exit(1);\n");
                    indent(o, ind + 1); fprintf(o, "}\n");
                    for (int i = 0; i < s->narms; i++) {
                        MatchArm *arm = &s->arms[i];
                        if (arm->variant) continue;      /* the `_` arm is emitted last */
                        indent(o, ind + 1); fprintf(o, "_m%d_a%d: {", mid, i);
                        gen_block(o, arm->body, arm->nbody, ind + 2, scope, ret);
                        indent(o, ind + 1); fprintf(o, "}");
                        if (!block_ends_in_return(arm->body, arm->nbody)) {
                            indent(o, ind + 1); fprintf(o, "goto _m%d_done;\n", mid);
                        } else {
                            fprintf(o, "\n");
                        }
                    }
                    if (wildidx >= 0) {
                        indent(o, ind + 1); fprintf(o, "_m%d_a%d: {", mid, wildidx);
                        gen_block(o, s->arms[wildidx].body, s->arms[wildidx].nbody,
                                  ind + 2, scope, ret);
                        indent(o, ind + 1); fprintf(o, "}\n");
                    }
                    indent(o, ind + 1); fprintf(o, "_m%d_done: ;\n", mid);
                } else {
                    int ncond = 0;
                    for (int i = 0; i < s->narms; i++) {
                        MatchArm *arm = &s->arms[i];
                        if (arm->variant) continue;
                        char *cond = sfmt("%s", "");
                        for (int k = 0; k < arm->pn; k++) {
                            char *piece = (arm->plo[k] == arm->phi[k])
                                ? sfmt("_m%d == %lld", mid, (long long)arm->plo[k])
                                : sfmt("_m%d >= %lld && _m%d <= %lld", mid,
                                       (long long)arm->plo[k], mid,
                                       (long long)arm->phi[k]);
                            char *old = cond;
                            cond = sfmt("%s%s%s", old, k ? " || " : "", piece);
                        }
                        indent(o, ind + 1);
                        fprintf(o, "%sif (%s) {\n", ncond ? "else " : "", cond);
                        ncond++;
                        gen_block(o, arm->body, arm->nbody, ind + 2, scope, ret);
                        indent(o, ind + 1); fprintf(o, "}\n");
                    }
                    if (ncond == 0) {   /* only a `_` arm: no if to else */
                        indent(o, ind + 1); fprintf(o, "{\n");
                        gen_block(o, wildarm->body, wildarm->nbody, ind + 2, scope, ret);
                        indent(o, ind + 1); fprintf(o, "}\n");
                    } else if (wildarm) {
                        indent(o, ind + 1); fprintf(o, "else {\n");
                        gen_block(o, wildarm->body, wildarm->nbody, ind + 2, scope, ret);
                        indent(o, ind + 1); fprintf(o, "}\n");
                    } else {
                        indent(o, ind + 1);
                        fprintf(o, "else { fprintf(stderr, \"tycho: non-exhaustive match\\n\"); exit(1); }\n");
                    }
                }
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
            /* a three-clause loop's post clause sits after the body inside the
             * while, so `continue` jumps to it instead of over it */
            { int pid = s->kind == S_CONTINUE ? g_loop_post[g_loop_depth - 1] : -1;
              indent(o, ind);
              if (pid >= 0) fprintf(o, "goto _post%d;\n", pid);
              else           fprintf(o, "%s;\n", s->kind == S_BREAK ? "break" : "continue"); }
            break;
        }
        case S_WHILE: {
            int id = g_blk++;
            indent(o, ind); fprintf(o, "{\n");
            indent(o, ind + 1); fprintf(o, "Arena _scr%d = arena_child(%s); _scr%d.name = \"%s:%d\";\n", id, scope, id, g_cur_proc_name, g_loop_lbl++);
            int _fo = fuse_open(o, s->body, s->nbody, ind + 1, s->expr);
            char *c = cond_unwrap(gen_expr(s->expr, scope));
            indent(o, ind + 1); fprintf(o, "while (%s) {\n", c);
            indent(o, ind + 2); fprintf(o, "arena_reset(&_scr%d);\n", id);
            char *ss = sfmt("&_scr%d", id);
            ascope_push(ss);   /* a return inside the loop must free _scrN too */
            g_loop_taskmark[g_loop_depth] = g_ntaskvars;   /* break/continue finish tasks above this */
            g_loop_post[g_loop_depth] = -1;                /* no post clause: `continue` is a C continue */
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
        case S_FOR3: {   /* `for init; cond; post:` */
            int id = g_blk++;
            int m = cv_mark();
            char *ss = sfmt("&_scr%d", id);
            indent(o, ind); fprintf(o, "{\n");
            /* init runs once, inside the loop's own C block and in the ENCLOSING
             * arena: the C block scoping is what makes the loop variable die with
             * the loop, and the outer arena is what an assignment to it (from the
             * body or from the post clause) will target via cv_arena -- so its
             * storage must not be the per-iteration scratch that arena_reset
             * recycles. An if-body decl is placed the same way. */
            gen_stmt(o, s->els[0], ind + 1, scope, ret);
            indent(o, ind + 1); fprintf(o, "Arena _scr%d = arena_child(%s); _scr%d.name = \"%s:%d\";\n", id, scope, id, g_cur_proc_name, g_loop_lbl++);
            int _fo = fuse_open(o, s->body, s->nbody, ind + 1, s->expr);
            char *c = cond_unwrap(gen_expr(s->expr, scope));
            indent(o, ind + 1); fprintf(o, "while (%s) {\n", c);
            indent(o, ind + 2); fprintf(o, "arena_reset(&_scr%d);\n", id);
            ascope_push(ss);   /* a return inside the loop must free _scrN too */
            g_loop_taskmark[g_loop_depth] = g_ntaskvars;   /* break/continue finish tasks above this */
            g_loop_post[g_loop_depth] = id;                /* `continue` -> goto _postN */
            g_loop_depth++;    /* moves are unsafe inside a loop (single read runs N times) */
            /* bounds-check elision: `for i := 0; i < len(A); i += 1:` proves
             * A[i] in-range for the body, on the shape proved by
             * for3_elidable_arr (which also runs stmts_unsafe over the body). */
            const char *el_arr = for3_elidable_arr(s);
            if (el_arr) {
                g_elide[g_nelide].iv = s->els[0]->name;
                g_elide[g_nelide].arr = el_arr;
                g_nelide++;
            }
            gen_block(o, s->body, s->nbody - 1, ind + 2, ss, ret);
            if (body_continues(s->body, s->nbody - 1)) { indent(o, ind + 2); fprintf(o, "_post%d: ;\n", id); }
            gen_stmt(o, s->body[s->nbody - 1], ind + 2, ss, ret);   /* the post clause, every iteration */
            if (el_arr) g_nelide--;
            g_loop_depth--;
            g_nascope--;
            indent(o, ind + 1); fprintf(o, "}\n");
            fuse_close(o, _fo, ind + 1);   /* break falls through to here; flush the cursors */
            indent(o, ind + 1); fprintf(o, "arena_free(&_scr%d);\n", id);
            indent(o, ind); fprintf(o, "}\n");
            cv_restore(m);     /* the init variable leaves scope with the loop */
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
            /* no `step` local: the loop always advances by 1 (the loops-cleanup plan). */
            char *ss = sfmt("&_scr%d", id);
            indent(o, ind); fprintf(o, "{\n");
            indent(o, ind + 1); fprintf(o, "Arena _scr%d = arena_child(%s); _scr%d.name = \"%s:%d\";\n", id, scope, id, g_cur_proc_name, g_loop_lbl++);
            int _fo = fuse_open(o, s->body, s->nbody, ind + 1, NULL);   /* bounds eval once, pre-loop */
            indent(o, ind + 1); fprintf(o, "tycho_int _stop%d = %s;\n", id, stop);
            /* `_stepN`, its `tycho: range step is zero` abort and the `_stepN > 0 ? ... : ...`
             * direction ternary went with the field on 2026-07-30 (the loops-cleanup plan). */
            indent(o, ind + 1);
            fprintf(o, "for (tycho_int h_%s = %s; h_%s < _stop%d; h_%s += 1) {\n", s->name, start, s->name, id, s->name);
            indent(o, ind + 2); fprintf(o, "arena_reset(&_scr%d);\n", id);
            int m = cv_mark();
            cv_push(s->name, ss);   /* loop var is an int value; owner is irrelevant but tracked */
            ascope_push(ss);        /* a return inside the loop must free _scrN too */
            g_loop_taskmark[g_loop_depth] = g_ntaskvars;   /* break/continue finish tasks above this */
            g_loop_post[g_loop_depth] = -1;                /* no post clause: `continue` is a C continue */
            g_loop_depth++;         /* moves are unsafe inside a loop (single read runs N times) */
            /* bounds-check elision: `for i in range(len(A)):` proves A[i] in-range
             * for the body, provided the body never reassigns/shadows A or i and
             * never passes A whole to a call (see stmt_unsafe). */
            int elide_pushed = 0;
            if (elision_on() && s->r_start->kind == E_INT && s->r_start->ival == 0 &&
                s->r_stop->kind == E_CALL && s->r_stop->sval &&
                !strcmp(s->r_stop->sval, "len") && s->r_stop->nargs == 1 &&
                s->r_stop->args[0]->kind == E_IDENT && g_nelide < 64 &&
                !IS_BOUNDED(s->r_stop->args[0]->type) &&   /* bounded stores in .v, not .data — elision emits .data[i], so never elide it */
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

/* FFI: forward-declare the C symbol with its real C ABI — no arena, no h_
 * prefix. This is enough to call it; no header #include needed. */
static void gen_extern_proto(FILE *o, Proc *pr) {
    /* bytes crosses as (ptr,len): a `bytes` parameter becomes two C args
     * (const unsigned char*, long); a `bytes` return becomes a void function with
     * two trailing out-params (unsigned char** out, long* outlen) — the out-param
     * shim convention. */
    int bret = (pr->ret == T_BYTES);
    /* an array return (`-> [int]`/`-> [float]`) uses the same out-param convention
     * as bytes: a void C fn with two trailing out-params (T** out, long* outlen). */
    const char *aret = ffi_arr_ptr_ctype(pr->ret);
    /* FFI R3a: an `Option(string)` return is a char* at the C ABI (the wrapper at
     * the call site turns NULL into None); declare the real C return, not TychoOpt. */
    int optstr = (IS_OPT(pr->ret) && opt_inner(pr->ret) == T_STRING);
    fprintf(o, "extern %s%s(", (bret || aret) ? "void " : optstr ? "char *" : pr->ret_ffi_ct ? pr->ret_ffi_ct : c_type(pr->ret), pr->name);
    int emitted = 0;
    for (int i = 0; i < pr->nparams; i++) {
        const char *arrp = ffi_arr_ptr_ctype(pr->params[i].type);
        if (pr->params[i].type == T_BYTES)
            fprintf(o, "%sconst unsigned char *, tycho_int", emitted++ ? ", " : "");
        else if (arrp)                     /* [int]/[float] cross as (const T*, long) */
            fprintf(o, "%s%s, tycho_int", emitted++ ? ", " : "", arrp);
        else if (pr->params[i].is_inout)   /* FFI R4: out-param — the C fn takes a pointer to T */
            fprintf(o, "%s%s*", emitted++ ? ", " : "", c_type(pr->params[i].type));
        else
            fprintf(o, "%s%s", emitted++ ? ", " : "", pr->params[i].ffi_ct ? pr->params[i].ffi_ct : c_type(pr->params[i].type));
    }
    if (bret) fprintf(o, "%sunsigned char **, tycho_int *", emitted++ ? ", " : "");
    else if (aret) fprintf(o, "%s%s **, tycho_int *", emitted++ ? ", " : "", pr->ret == T_ARRAY_INT ? "tycho_int" : "double");
    if (emitted == 0) fprintf(o, "void");
    fprintf(o, ");\n");
}

static void gen_proc(FILE *o, Proc *pr) {
    diag_use_proc(pr);   /* package mode: codegen errors name THIS proc's file */
    gen_signature(o, pr);
    fprintf(o, " {\n");
    /* stamp the residency label (TYCHO_ARENA_STATS): every arena opened inside this
     * proc inherits it through arena_child, so allocations report per function. */
    indent(o, 1); fprintf(o, "Arena _scope = arena_child(_parent); _scope.name = \"%s\";\n", pr->name);
    g_cur_proc_name = pr->name;
    g_loop_lbl = 0;
    g_gen_ret = pr->ret;
    g_proc_body = pr->body; g_proc_nbody = pr->nbody;   /* for move-on-last-use read counts */
    g_loop_depth = 0;
    g_ncv = 0;
    g_nascope = 0;   /* no enclosing block arenas at the proc body top level */
    g_ntaskvars = 0; /* CC-2: no live tasks at proc entry */
    /* return-slot optimization: which top-level locals escape via return */
    g_nesc = 0;
    collect_escapes(pr->body, pr->nbody);
    collect_ret_alias(pr);   /* + return-only-escaping embedded locals (precise, no retention) */
    /* in-place append: which string locals are self-append accumulators */
    g_naccum = 0;
    collect_accums(pr->body, pr->nbody);
    /* the string accumulator opt declares its sidecar len/cap locals at the
     * variable's S_DECL; a by-value parameter has no S_DECL, so a self-append on
     * a string param (`s = s + e`) must NOT take the in-place path — its
     * sidecars would be undeclared C. Drop NON-inout params from the accumulator
     * set; they fall back to ordinary concat/pure-set-and-rebind, which is
     * correct. A inout param is KEPT: it carries no string sidecars (inout
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
 * be emitted in containment order (DYNAMIC composite-array typedefs are emitted
 * before this — they hold only a pointer, so they break cycles). A DFS with
 * colouring (0 unvisited, 1 on-stack, 2 done) emits each in dependency order; a
 * back-edge is an infinite type (a struct that contains itself by value), which
 * is a real error — use an array or `Option([T])` for indirection.
 *
 * INLINE-STORAGE arrays — `[N]T` (fixed) and `bounded[N]T` — are in this DFS too,
 * because `struct TychoArrC<n>_ { T v[N]; ... }` needs T *complete*, not merely
 * forward-declared. Emitting them with the pointer-shaped arrays (before struct /
 * Option / Result / tuple / soa bodies) is what made `[2]Pt`, `bounded[4]Pt`,
 * `bounded[4](int,int)`, `bounded[4]Option(int)` and `bounded[4]soa[Pt]` emit C
 * that cc rejects with "array type has incomplete element type". The dependency
 * really is mutual — a struct may hold a `[2]Pt` field by value and that array
 * holds `Pt` by value — so one dependency-ordered pass, not two fixed phases. */
static int *g_struct_color; static int g_struct_color_cap;
static int *g_opt_color;    static int g_opt_color_cap;
static int *g_res_color;    static int g_res_color_cap;
static int *g_tup_color;    static int g_tup_color_cap;
static int *g_arrc_color;   static int g_arrc_color_cap;
static int g_emit_line;

/* Does a by-value member of type `t` need its own C body emitted FIRST? True for
 * the four aggregate families and for an inline-storage array (`size > 0` covers
 * both `[N]T` and `bounded[N]T`; a `[$N]T` size-param encodes NEGATIVE and is
 * template-only, and a dynamic `[T]` is size == 0 and holds a pointer). */
static int inline_arrc(Type t) { return IS_ARRC(t) && g_arrtypes[ARRC_ID(t)].size > 0; }
static int needs_body_first(Type t) {
    t = base_of(t);   /* a newtype is zero-cost: c_type(newtype) IS the underlying's C type,
                       * so `type C = [2]int` as a struct field / tuple element needs the
                       * inline array's body first, exactly as a bare `[2]int` would. */
    return IS_STRUCT(t) || IS_OPT(t) || IS_RES(t) || IS_TUP(t) || inline_arrc(t);
}

static void emit_aggregate(FILE *o, Type t) {
    t = base_of(t);   /* see needs_body_first: a newtype carries no C body of its own */
    if (IS_ARRC(t) && !inline_arrc(t)) return;   /* a dynamic `[T]` / a `[$N]T` template holds a POINTER: its body is emitted with step (2b) */
    if (has_typaram(t)) return;   /* generics: a type mentioning `$T` (from a template) is transient -- never emitted */
    if (IS_ARRC(t)) {   /* [N]T / bounded[N]T: the element is stored INLINE, so its body must precede this one */
        int id = ARRC_ID(t);
        if (g_arrc_color[id] == 2) return;
        if (g_arrc_color[id] == 1)
            die_at(g_emit_line, "infinite type: %s contains itself by value — "
                   "use a dynamic array ([%s]) for indirection",
                   type_name(t), type_name(arr_elem(t)));
        g_arrc_color[id] = 1;
        Type el = g_arrtypes[id].elem;
        if (needs_body_first(el)) emit_aggregate(o, el);
        g_arrc_color[id] = 2;
        if (o) {
            if (g_arrtypes[id].bnd)   /* bounded[N]T: inline storage + a runtime count */
                fprintf(o, "struct TychoArrC%d_ { %s v[%lld]; tycho_int len; };\n",
                        id, c_type(el), (long long)g_arrtypes[id].size);
            else                      /* fixed-size [N]T (1.6): inline storage, no heap descriptor */
                fprintf(o, "struct TychoArrC%d_ { %s v[%lld]; };\n",
                        id, c_type(el), (long long)g_arrtypes[id].size);
        }
        return;
    }
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
            if (needs_body_first(ft)) emit_aggregate(o, ft);
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
        if (needs_body_first(inner)) emit_aggregate(o, inner);
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
        if (needs_body_first(okt))  emit_aggregate(o, okt);
        if (needs_body_first(errt)) emit_aggregate(o, errt);
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
            if (needs_body_first(et)) emit_aggregate(o, et);
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
    TBL_RESERVE(g_arrc_color,   g_narrtypes, g_arrc_color_cap);
    for (int i = 0; i < g_nstructs; i++)  g_struct_color[i] = 0;
    for (int i = 0; i < g_nopttypes; i++) g_opt_color[i] = 0;
    for (int i = 0; i < g_nrestypes; i++) g_res_color[i] = 0;
    for (int i = 0; i < g_ntuptypes; i++) g_tup_color[i] = 0;
    for (int i = 0; i < g_narrtypes; i++) g_arrc_color[i] = 0;
    g_emit_line = 0;
    for (int i = 0; i < g_nstructs; i++)  emit_aggregate(NULL, STRUCT_TYPE(i));
    for (int i = 0; i < g_nopttypes; i++) emit_aggregate(NULL, T_OPT_BASE + i);
    for (int i = 0; i < g_nrestypes; i++) emit_aggregate(NULL, T_RES_BASE + i);
    for (int i = 0; i < g_ntuptypes; i++) emit_aggregate(NULL, T_TUP_BASE + i);
    for (int i = 0; i < g_narrtypes; i++) emit_aggregate(NULL, T_ARRC_BASE + i);   /* [N]T / bounded[N]T hold the element by value too */
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
    /* no r_step to clone: every S_FORRANGE steps by 1 (the loops-cleanup plan). */
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
        p->params[j] = gi->tmpl->params[j];   /* same name + inout flag */
        p->params[j].type = gi->params[j];     /* bound concrete type */
    }
    p->nparams = gi->nparams;
    p->body = gi->body; p->nbody = gi->nbody;   /* Stage-2: resolve+emit the instance's OWN cloned body */
    return p;
}

/* ---- compact-dict (indexed-dict) map emit helpers: shared by every TychoMapC
 * key variant (composite / int-rep / string); each preserves that variant's
 * exact key hashing, matching, and copy semantics. ---- */
static const char *mapc_kslot(Type k) {   /* C type of one key slot in ekeys[] */
    if (mapkey_intrep(k)) return "tycho_int ";
    if (!mapkey_composite(k)) return "char *";
    return c_type(k);
}
static char *mapc_kparam(Type k) {        /* the key function parameter (READ-only fns) */
    if (mapkey_intrep(k)) return sfmt("tycho_int k");
    if (!mapkey_composite(k)) return sfmt("const char *k");
    return sfmt("%sk", c_type(k));
}
/* The key parameter of a function that STORES the key into an ekeys[] slot (_append
 * only). mapc_kparam is `const char *` for string keys -- correct for the read-only
 * fns (find/get/has/del/put all only hash, compare, or copy it), but a lie for
 * _append, whose contract is ownership transfer: its two callers (_put and
 * _slotptr) hand it the freshly arena-owned copy from mapc_kcopy, and it stores
 * that pointer into m->ekeys[e], a `char *` slot (mapc_kslot). Passing an owned
 * pointer through a `const char *` parameter is what made the store discard the
 * qualifier (-Wdiscarded-qualifiers). The slot is NOT made const: c_type(T_STRING)
 * is "char *" (:1223) and every string container in the system -- TychoArrStr.data,
 * the runtime's own TychoMapSI.ekeys -- is `char **`; tycho_map_si_append, the
 * hand-written twin this family mirrors, likewise takes `char *k`. So the owning
 * param is exactly the slot type, which is also the invariant tychoc0 states
 * directly (`kpar := kslot + " k"`, tychoc0.ty:10475). Nothing writes THROUGH the
 * slot -- trace under the front-door plan. */
static char *mapc_kparam_own(Type k) {
    return sfmt("%sk", mapc_kslot(k));
}
static char *mapc_khash(Type k, const char *e) {   /* hash of a key expression e */
    if (mapkey_composite(k)) return gen_hash(k, e);
    if (mapkey_intrep(k))    return sfmt("tycho_ik_hash(%s)", e);
    return sfmt("tycho_si_hash(%s)", e);
}
static char *mapc_kmatch(Type k, const char *ek, const char *q) {   /* stored key ek == query q */
    if (mapkey_composite(k)) return gen_eq(k, ek, q);
    if (mapkey_intrep(k))    return sfmt("%s == %s", ek, q);
    return sfmt("strcmp(%s, %s) == 0", ek, q);
}
static char *mapc_kcopy(Type k) {   /* own the key k into arena a on insert */
    if (mapkey_composite(k)) return copy_into(k, "a", "k");
    if (mapkey_intrep(k))    return sfmt("k");
    return sfmt("tycho_str_copy(a, k)");
}
static char *mapc_keyelem(Type k) {   /* keys() element rebuilt from ekeys[e] */
    if (mapkey_intrep(k) && IS_ENUM(k)) return sfmt("_sing_tab_%s[m.ekeys[e]]", g_enums[ENUM_ID(k)].name);
    return sfmt("m.ekeys[e]");
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
        for (int k = 0; k < g_ginsts[i].nsp; k++) {   /* const generics 1.6B: bind each `$N` as an int const so the body's `N` folds to the instance's length */
            Expr *lit = new_expr(E_INT, p->line);
            lit->ival = g_ginsts[i].spvals[k];
            vars_push_const(g_ginsts[i].tmpl->sizeparams[k], T_INT, lit);
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
    for (int i = 0; i < g_narrtypes; i++)           /* (2b) DYNAMIC composite-array bodies (tags forward-declared above). */
        if (!inline_arrc(T_ARRC_BASE + i))          /*      The element is behind a `*data` pointer, so its forward */
            fprintf(o, "struct TychoArrC%d_ { %s*data; tycho_int len; tycho_int cap; };\n",   /* decl suffices. */
                    i, c_type(g_arrtypes[i].elem));
    /* (2b-inline) `[N]T` and `bounded[N]T` store the element INLINE, so their
     * bodies need it COMPLETE — they are emitted in step (3) below, inside the
     * containment DFS, alongside the struct/Option/Result/tuple bodies. */
    for (int i = 0; i < g_nmaptypes; i++)           /* (2b') composite-map bodies [K: V] -- COMPACT indexed-dict: int32 index table -> dense insertion-ordered entries */
        fprintf(o, "struct TychoMapC%d_ { %s*ekeys; %s*evals; unsigned char *elive; int *idx; tycho_int len; tycho_int ecount; tycho_int ecap; tycho_int icap; };\n",
                i, mapc_kslot(g_maptypes[i].key), c_type(g_maptypes[i].val));
    /* (2c) soa typedefs: one field-buffer POINTER per struct field + len/cap.
     * Members are pointers, so the element struct's tag forward-decl above is
     * enough — this can precede struct bodies, letting a struct embed a soa by
     * value. The push/copy/eq BODIES (which need sizeof field types) stay later. */
    for (int i = 0; i < g_nsoatypes; i++) {
        StructDef *sd = &g_structs[STRUCT_ID(g_soatypes[i].st)];
        fprintf(o, "typedef struct {");
        for (int f = 0; f < sd->nfields; f++)
            fprintf(o, " %s*f%d;", c_type(sd->fields[f].type), f);
        fprintf(o, " tycho_int len; tycho_int cap; } Soa%d;\n", i);
        fprintf(o, "static tycho_int Soa%d_bound(Soa%d *s, tycho_int i) { if (i < 0 || i >= s->len) { "
                   "fprintf(stderr, \"tycho: index %%\" TY_PRId \" out of bounds (len %%\" TY_PRId \")\\n\", i, s->len); exit(1); } return i; }\n", i, i);
    }
    fputs("\n", o);
    /* (3) struct bodies, Option typedefs AND the inline-storage array bodies
     * (`[N]T`, `bounded[N]T`) in containment order (infinite types are rejected
     * here). Enum descriptors above are complete, so a struct/Option may embed an
     * enum by value (it's only 2 words); the soa typedefs and map bodies just
     * above are complete too, so `bounded[4]soa[Pt]` and `bounded[4][string:int]`
     * resolve here. */
    TBL_RESERVE(g_struct_color, g_nstructs,  g_struct_color_cap);
    TBL_RESERVE(g_opt_color,    g_nopttypes, g_opt_color_cap);
    TBL_RESERVE(g_res_color,    g_nrestypes, g_res_color_cap);
    TBL_RESERVE(g_tup_color,    g_ntuptypes, g_tup_color_cap);
    TBL_RESERVE(g_arrc_color,   g_narrtypes, g_arrc_color_cap);
    for (int i = 0; i < g_nstructs; i++)  g_struct_color[i] = 0;
    for (int i = 0; i < g_nopttypes; i++) g_opt_color[i] = 0;
    for (int i = 0; i < g_nrestypes; i++) g_res_color[i] = 0;
    for (int i = 0; i < g_ntuptypes; i++) g_tup_color[i] = 0;
    for (int i = 0; i < g_narrtypes; i++) g_arrc_color[i] = 0;
    g_emit_line = 0;
    for (int i = 0; i < g_nstructs; i++)  emit_aggregate(o, STRUCT_TYPE(i));
    for (int i = 0; i < g_nopttypes; i++) emit_aggregate(o, T_OPT_BASE + i);
    for (int i = 0; i < g_nrestypes; i++) emit_aggregate(o, T_RES_BASE + i);
    for (int i = 0; i < g_ntuptypes; i++) emit_aggregate(o, T_TUP_BASE + i);
    for (int i = 0; i < g_narrtypes; i++) emit_aggregate(o, T_ARRC_BASE + i);   /* inline arrays not reached from an aggregate above */
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
        if (struct_keyused(STRUCT_TYPE(i)) || hash_keyused(STRUCT_TYPE(i)))   /* composite map key / hash(x): deep hash */
            fprintf(o, "static uint64_t tycho_hash_S_%s(S_%s v);\n", nm, nm);
        if (hash_keyused(STRUCT_TYPE(i)))       /* hash(x): the deterministic twin */
            fprintf(o, "static uint64_t tycho_dhash_S_%s(S_%s v);\n", nm, nm);
    }
    for (int i = 0; i < g_ntuptypes; i++)           /* (4) tuple deep-hash prototypes (composite map keys; emitted before bodies so struct/tuple hashes can reference each other) */
        if (struct_keyused(T_TUP_BASE + i) || hash_keyused(T_TUP_BASE + i))
            fprintf(o, "static uint64_t tycho_hash_T%d(TychoTup%d v);\n", i, i);
    for (int i = 0; i < g_ntuptypes; i++)
        if (hash_keyused(T_TUP_BASE + i))
            fprintf(o, "static uint64_t tycho_dhash_T%d(TychoTup%d v);\n", i, i);
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
        if (g_arrtypes[i].bnd) {                       /* bounded[N]T: read/copy/eq like a fixarr, PLUS a trap-on-full push */
            fprintf(o, "static void tycho_arr_C%d_push(Arena*, TychoArrC%d*, %s);\n", i, i, ct);
            fprintf(o, "static %stycho_arr_C%d_get(TychoArrC%d, tycho_int);\n", ct, i, i);
            fprintf(o, "static %s*tycho_arr_C%d_ptr(TychoArrC%d*, tycho_int);\n", ct, i, i);
            fprintf(o, "static void tycho_arr_C%d_set(Arena*, TychoArrC%d*, tycho_int, %s);\n", i, i, ct);
            fprintf(o, "static TychoArrC%d tycho_arr_C%d_copy(Arena*, TychoArrC%d);\n", i, i, i);
            fprintf(o, "static int tycho_arr_C%d_eq(TychoArrC%d, TychoArrC%d);\n", i, i, i);
            if (struct_keyused(T_ARRC_BASE + i) || hash_keyused(T_ARRC_BASE + i))
                fprintf(o, "static uint64_t tycho_arr_C%d_hash(TychoArrC%d);\n", i, i);
            if (hash_keyused(T_ARRC_BASE + i))
                fprintf(o, "static uint64_t tycho_arr_C%d_dhash(TychoArrC%d);\n", i, i);
            continue;
        }
        if (g_arrtypes[i].size > 0) {                 /* fixed [N]T: only read/copy/eq -- no growth */
            fprintf(o, "static %stycho_arr_C%d_get(TychoArrC%d, tycho_int);\n", ct, i, i);
            fprintf(o, "static %s*tycho_arr_C%d_ptr(TychoArrC%d*, tycho_int);\n", ct, i, i);
            fprintf(o, "static void tycho_arr_C%d_set(Arena*, TychoArrC%d*, tycho_int, %s);\n", i, i, ct);
            fprintf(o, "static TychoArrC%d tycho_arr_C%d_copy(Arena*, TychoArrC%d);\n", i, i, i);
            fprintf(o, "static int tycho_arr_C%d_eq(TychoArrC%d, TychoArrC%d);\n", i, i, i);
            if (struct_keyused(T_ARRC_BASE + i) || hash_keyused(T_ARRC_BASE + i))
                fprintf(o, "static uint64_t tycho_arr_C%d_hash(TychoArrC%d);\n", i, i);
            if (hash_keyused(T_ARRC_BASE + i))
                fprintf(o, "static uint64_t tycho_arr_C%d_dhash(TychoArrC%d);\n", i, i);
            continue;
        }
        fprintf(o, "static TychoArrC%d tycho_arr_C%d_with_cap(Arena*, tycho_int);\n", i, i);
        fprintf(o, "static void tycho_arr_C%d_push(Arena*, TychoArrC%d*, %s);\n", i, i, ct);
        fprintf(o, "static void tycho_arr_C%d_reserve(Arena*, TychoArrC%d*, tycho_int);\n", i, i);
        fprintf(o, "static void tycho_arr_C%d_grow(Arena*, %s**, tycho_int*, tycho_int);\n", i, ct);
        fprintf(o, "static %stycho_arr_C%d_pop(Arena*, TychoArrC%d*);\n", ct, i, i);
        fprintf(o, "static %stycho_arr_C%d_get(TychoArrC%d, tycho_int);\n", ct, i, i);
        fprintf(o, "static %s*tycho_arr_C%d_ptr(TychoArrC%d*, tycho_int);\n", ct, i, i);
        fprintf(o, "static void tycho_arr_C%d_set(Arena*, TychoArrC%d*, tycho_int, %s);\n", i, i, ct);
        fprintf(o, "static TychoArrC%d tycho_arr_C%d_copy(Arena*, TychoArrC%d);\n", i, i, i);
        fprintf(o, "static int tycho_arr_C%d_eq(TychoArrC%d, TychoArrC%d);\n", i, i, i);
        if (struct_keyused(T_ARRC_BASE + i) || hash_keyused(T_ARRC_BASE + i))   /* composite map key / hash(x): deep hash */
            fprintf(o, "static uint64_t tycho_arr_C%d_hash(TychoArrC%d);\n", i, i);
        if (hash_keyused(T_ARRC_BASE + i))       /* hash(x): the deterministic twin */
            fprintf(o, "static uint64_t tycho_arr_C%d_dhash(TychoArrC%d);\n", i, i);
    }
    for (int i = 0; i < g_nmaptypes; i++) {         /* (4) composite-map copy/eq prototypes: a struct/array/tuple FIELD of composite-map type calls these in its copy/eq body, which is emitted before the map family itself (7a') -- without the proto the struct copier sees an implicit declaration */
        if (has_typaram(T_MAPC_BASE + i)) continue;   /* generics: a `[$K: $V]` template map -- transient, never emitted */
        fprintf(o, "static TychoMapC%d tycho_mapc%d_copy(Arena*, TychoMapC%d);\n", i, i, i);
        fprintf(o, "static int tycho_mapc%d_eq(TychoMapC%d, TychoMapC%d);\n", i, i, i);
        fprintf(o, "static void tycho_mapc%d_idxput(TychoMapC%d*, tycho_int);\n", i, i);   /* the reserve body calls it */
    }
    for (int i = 0; i < g_nstructs; i++)            /* (4) str() prototypes (F5): forward so recursive/mutual str refs resolve */
        if (!g_structs[i].generic)
            fprintf(o, "static char *tycho_str_S_%s(Arena*, S_%s);\n", g_structs[i].name, g_structs[i].name);
    for (int i = 0; i < g_nenums; i++)
        if (!g_enums[i].generic)
            fprintf(o, "static char *tycho_str_E_%s(Arena*, E_%s*);\n", g_enums[i].name, g_enums[i].name);
    for (int i = 0; i < g_narrtypes; i++)
        if (!has_typaram(T_ARRC_BASE + i))
            fprintf(o, "static char *tycho_str_arr_C%d(Arena*, TychoArrC%d);\n", i, i);
    for (int i = 0; i < g_nmaptypes; i++)
        if (!has_typaram(T_MAPC_BASE + i))
            fprintf(o, "static char *tycho_str_mapc%d(Arena*, TychoMapC%d);\n", i, i);
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
        if (!struct_keyused(STRUCT_TYPE(i)) && !hash_keyused(STRUCT_TYPE(i))) continue;
        StructDef *sd = &g_structs[i];
        fprintf(o, "static uint64_t tycho_hash_S_%s(S_%s v) {\n", sd->name, sd->name);
        fprintf(o, "    uint64_t h = tycho_hash_k0;\n");
        for (int j = 0; j < sd->nfields; j++) {
            char *vf = sfmt("v.f_%s", sd->fields[j].name);
            fprintf(o, "    h = h * UINT64_C(1099511628211) ^ %s;\n", gen_hash(sd->fields[j].type, vf));
        }
        fprintf(o, "    return h;\n}\n\n");
        if (hash_keyused(STRUCT_TYPE(i))) {   /* the deterministic twin for hash(x): fixed seed, det leaves */
            g_hash_det = 1;
            fprintf(o, "static uint64_t tycho_dhash_S_%s(S_%s v) {\n", sd->name, sd->name);
            fprintf(o, "    uint64_t h = UINT64_C(0x9e3779b97f4a7c15);\n");
            for (int j = 0; j < sd->nfields; j++) {
                char *vf = sfmt("v.f_%s", sd->fields[j].name);
                fprintf(o, "    h = h * UINT64_C(1099511628211) ^ %s;\n", gen_hash(sd->fields[j].type, vf));
            }
            fprintf(o, "    return h;\n}\n\n");
            g_hash_det = 0;
        }
    }
    /* (6c) deep-hash body per tuple used as a (nested) composite map key. Tuple == is
     * inline in gen_eq, but the hash is a function (a stateful FNV fold); element
     * access is ._0/._1. Same seeded fold as the struct hash. */
    for (int i = 0; i < g_ntuptypes; i++) {
        if (!struct_keyused(T_TUP_BASE + i) && !hash_keyused(T_TUP_BASE + i)) continue;
        fprintf(o, "static uint64_t tycho_hash_T%d(TychoTup%d v) {\n", i, i);
        fprintf(o, "    uint64_t h = tycho_hash_k0;\n");
        for (int j = 0; j < tup_n(T_TUP_BASE + i); j++) {
            char *vf = sfmt("v._%d", j);
            fprintf(o, "    h = h * UINT64_C(1099511628211) ^ %s;\n", gen_hash(tup_elem(T_TUP_BASE + i, j), vf));
        }
        fprintf(o, "    return h;\n}\n\n");
        if (hash_keyused(T_TUP_BASE + i)) {
            g_hash_det = 1;
            fprintf(o, "static uint64_t tycho_dhash_T%d(TychoTup%d v) {\n", i, i);
            fprintf(o, "    uint64_t h = UINT64_C(0x9e3779b97f4a7c15);\n");
            for (int j = 0; j < tup_n(T_TUP_BASE + i); j++) {
                char *vf = sfmt("v._%d", j);
                fprintf(o, "    h = h * UINT64_C(1099511628211) ^ %s;\n", gen_hash(tup_elem(T_TUP_BASE + i, j), vf));
            }
            fprintf(o, "    return h;\n}\n\n");
            g_hash_det = 0;
        }
    }
    /* (7) composite-array op bodies (typedef already emitted in step 2). Each
     * deep-copies its elements through the same seam as the [string] array. */
    for (int i = 0; i < g_narrtypes; i++) {
        if (has_typaram(T_ARRC_BASE + i)) continue;   /* generics: `[$T]` from a template -- transient */
        Type et = g_arrtypes[i].elem;
        const char *ct = c_type(et);              /* element C type (trailing space) */
        if (g_arrtypes[i].bnd) {                  /* bounded[N]T: inline v[N] + runtime len; push traps on overflow */
            int64_t n = g_arrtypes[i].size;          /* capacity */
            fprintf(o,   /* push: trap when full instead of growing (the whole point of a bounded) */
                "static void tycho_arr_C%d_push(Arena *a, TychoArrC%d *xs, %sv) {\n"
                "    if (xs->len >= %lld) { fprintf(stderr, \"tycho: push to a full bounded[%lld]\\n\"); exit(1); }\n"
                "    xs->v[xs->len++] = %s;\n}\n", i, i, ct, (long long)n, (long long)n, copy_into(et, "a", "v"));
            fprintf(o,
                "static %stycho_arr_C%d_get(TychoArrC%d xs, tycho_int i) {\n"
                "    if (i < 0 || i >= xs.len) { fprintf(stderr, \"tycho: index %%\" TY_PRId \" out of bounds (len %%\" TY_PRId \")\\n\", i, xs.len); exit(1); }\n"
                "    return xs.v[i];\n}\n", ct, i, i);
            fprintf(o,
                "static %s*tycho_arr_C%d_ptr(TychoArrC%d *xs, tycho_int i) {\n"
                "    if (i < 0 || i >= xs->len) { fprintf(stderr, \"tycho: index %%\" TY_PRId \" out of bounds (len %%\" TY_PRId \")\\n\", i, xs->len); exit(1); }\n"
                "    return &xs->v[i];\n}\n", ct, i, i);
            fprintf(o,
                "static void tycho_arr_C%d_set(Arena *a, TychoArrC%d *xs, tycho_int i, %sv) {\n"
                "    if (i < 0 || i >= xs->len) { fprintf(stderr, \"tycho: index %%\" TY_PRId \" out of bounds (len %%\" TY_PRId \")\\n\", i, xs->len); exit(1); }\n"
                "    xs->v[i] = %s;\n}\n", i, i, ct, copy_into(et, "a", "v"));
            fprintf(o,   /* copy: struct assign carries v[] + len; deep-copy the live elements */
                "static TychoArrC%d tycho_arr_C%d_copy(Arena *a, TychoArrC%d src) {\n"
                "    TychoArrC%d r = src;\n"
                "    for (tycho_int i = 0; i < src.len; i++) r.v[i] = %s;\n"
                "    return r;\n}\n", i, i, i, i, copy_into(et, "a", "src.v[i]"));
            fprintf(o,
                "static int tycho_arr_C%d_eq(TychoArrC%d x, TychoArrC%d y) {\n"
                "    if (x.len != y.len) return 0;\n"
                "    for (tycho_int i = 0; i < x.len; i++) if (!(%s)) return 0;\n"
                "    return 1;\n}\n\n", i, i, i, gen_eq(et, "x.v[i]", "y.v[i]"));
            if (struct_keyused(T_ARRC_BASE + i) || hash_keyused(T_ARRC_BASE + i)) {
                fprintf(o,
                    "static uint64_t tycho_arr_C%d_hash(TychoArrC%d xs) {\n"
                    "    uint64_t h = UINT64_C(1469598103934665603);\n"
                    "    for (tycho_int i = 0; i < xs.len; i++) { uint64_t e = %s; h = (h ^ e) * UINT64_C(1099511628211); }\n"
                    "    return h;\n}\n", i, i, "(uint64_t)xs.v[i]");
            }
            if (hash_keyused(T_ARRC_BASE + i)) {
                fprintf(o,
                    "static uint64_t tycho_arr_C%d_dhash(TychoArrC%d xs) {\n"
                    "    uint64_t h = UINT64_C(0x9e3779b97f4a7c15);\n"
                    "    for (tycho_int i = 0; i < xs.len; i++) { uint64_t e = %s; h = (h ^ e) * UINT64_C(1099511628211); }\n"
                    "    return h;\n}\n", i, i, "(uint64_t)xs.v[i]");
            }
            continue;
        }
        if (g_arrtypes[i].size > 0) {             /* fixed-size [N]T (1.6): inline; read/copy/eq only */
            int64_t n = g_arrtypes[i].size;
            fprintf(o,
                "static %stycho_arr_C%d_get(TychoArrC%d xs, tycho_int i) {\n"
                "    if (i < 0 || i >= %lld) { fprintf(stderr, \"tycho: index %%\" TY_PRId \" out of bounds (len %lld)\\n\", i); exit(1); }\n"
                "    return xs.v[i];\n}\n", ct, i, i, (long long)n, (long long)n);
            fprintf(o,
                "static %s*tycho_arr_C%d_ptr(TychoArrC%d *xs, tycho_int i) {\n"
                "    if (i < 0 || i >= %lld) { fprintf(stderr, \"tycho: index %%\" TY_PRId \" out of bounds (len %lld)\\n\", i); exit(1); }\n"
                "    return &xs->v[i];\n}\n", ct, i, i, (long long)n, (long long)n);
            fprintf(o,
                "static void tycho_arr_C%d_set(Arena *a, TychoArrC%d *xs, tycho_int i, %sv) {\n"
                "    if (i < 0 || i >= %lld) { fprintf(stderr, \"tycho: index %%\" TY_PRId \" out of bounds (len %lld)\\n\", i); exit(1); }\n"
                "    xs->v[i] = %s;\n}\n", i, i, ct, (long long)n, (long long)n, copy_into(et, "a", "v"));
            fprintf(o,   /* copy: the struct copy memcpys the inline array; deep-copy each element for a heap element type */
                "static TychoArrC%d tycho_arr_C%d_copy(Arena *a, TychoArrC%d src) {\n"
                "    TychoArrC%d r = src;\n"
                "    for (tycho_int i = 0; i < %lld; i++) r.v[i] = %s;\n"
                "    return r;\n}\n", i, i, i, i, (long long)n, copy_into(et, "a", "src.v[i]"));
            fprintf(o,
                "static int tycho_arr_C%d_eq(TychoArrC%d x, TychoArrC%d y) {\n"
                "    for (tycho_int i = 0; i < %lld; i++) if (!(%s)) return 0;\n"
                "    return 1;\n}\n\n", i, i, i, (long long)n, gen_eq(et, "x.v[i]", "y.v[i]"));
            if (struct_keyused(T_ARRC_BASE + i) || hash_keyused(T_ARRC_BASE + i)) {
                fprintf(o,
                    "static uint64_t tycho_arr_C%d_hash(TychoArrC%d xs) {\n"
                    "    uint64_t h = UINT64_C(1469598103934665603);\n"
                    "    for (tycho_int i = 0; i < %lld; i++) { uint64_t e = %s; h = (h ^ e) * UINT64_C(1099511628211); }\n"
                    "    return h;\n}\n", i, i, (long long)n, "(uint64_t)xs.v[i]");
            }
            if (hash_keyused(T_ARRC_BASE + i)) {
                fprintf(o,
                    "static uint64_t tycho_arr_C%d_dhash(TychoArrC%d xs) {\n"
                    "    uint64_t h = UINT64_C(0x9e3779b97f4a7c15);\n"
                    "    for (tycho_int i = 0; i < %lld; i++) { uint64_t e = %s; h = (h ^ e) * UINT64_C(1099511628211); }\n"
                    "    return h;\n}\n", i, i, (long long)n, "(uint64_t)xs.v[i]");
            }
            continue;
        }
        fprintf(o,
            "static TychoArrC%d tycho_arr_C%d_with_cap(Arena *a, tycho_int cap) {\n"
            "    TychoArrC%d r; r.len = 0; r.cap = cap;\n"
            "    r.data = cap > 0 ? (%s*)arena_alloc(a, (size_t)cap * sizeof(%s)) : 0;\n"
            "    return r;\n}\n", i, i, i, ct, ct);
        fprintf(o,
            "static void tycho_arr_C%d_push(Arena *a, TychoArrC%d *xs, %sv) {\n"
            "    if (xs->len == xs->cap) {\n"
            "        tycho_int nc = xs->cap ? xs->cap * 2 : 4;\n"
            "        %s*nd = (%s*)arena_alloc(a, (size_t)nc * sizeof(%s));\n"
            "        for (tycho_int i = 0; i < xs->len; i++) nd[i] = xs->data[i];\n"
            "        if (xs->cap) arena_recycle(a, xs->data, (size_t)xs->cap * sizeof(%s));\n"  /* dead spine; element heap lives on via nd */
            "        xs->data = nd; xs->cap = nc;\n    }\n"
            "    xs->data[xs->len++] = %s;\n}\n",
            i, i, ct, ct, ct, ct, ct, copy_into(et, "a", "v"));
        fprintf(o,   /* capacity hint (reserve): grow the spine to n if larger; elements' heap lives on via nd (shallow spine copy, like push's regrow) */
            "static void tycho_arr_C%d_reserve(Arena *a, TychoArrC%d *xs, tycho_int n) {\n"
            "    if (n <= xs->cap) return;\n"
            "    %s*nd = (%s*)arena_alloc(a, (size_t)n * sizeof(%s));\n"
            "    for (tycho_int i = 0; i < xs->len; i++) nd[i] = xs->data[i];\n"
            "    if (xs->cap) arena_recycle(a, xs->data, (size_t)xs->cap * sizeof(%s));\n"
            "    xs->data = nd; xs->cap = n;\n}\n", i, i, ct, ct, ct, ct);
        fprintf(o,   /* push-loop fusion grow hook: regrow the spine (shallow copy); elements were already deep-copied into `a` at each fused store */
            "static void tycho_arr_C%d_grow(Arena *a, %s**data, tycho_int *cap, tycho_int len) {\n"
            "    tycho_int nc = *cap ? *cap * 2 : 4;\n"
            "    %s*nd = (%s*)arena_alloc(a, (size_t)nc * sizeof(%s));\n"
            "    for (tycho_int i = 0; i < len; i++) nd[i] = (*data)[i];\n"
            "    if (*cap) arena_recycle(a, *data, (size_t)*cap * sizeof(%s));\n"
            "    *data = nd; *cap = nc;\n}\n",
            i, ct, ct, ct, ct, ct);
        fprintf(o,   /* pop: shrink + return the last element, deep-copied into `a` */
            "static %stycho_arr_C%d_pop(Arena *a, TychoArrC%d *xs) {\n"
            "    if (xs->len == 0) { fprintf(stderr, \"tycho: pop from an empty array\\n\"); exit(1); }\n"
            "    xs->len--;\n    return %s;\n}\n",
            ct, i, i, copy_into(et, "a", "xs->data[xs->len]"));
        fprintf(o,
            "static %stycho_arr_C%d_get(TychoArrC%d xs, tycho_int i) {\n"
            "    if (i < 0 || i >= xs.len) { fprintf(stderr, \"tycho: index %%\" TY_PRId \" out of bounds (len %%\" TY_PRId \")\\n\", i, xs.len); exit(1); }\n"
            "    return xs.data[i];\n}\n", ct, i, i);
        fprintf(o,   /* projection: a bounds-checked pointer into the buffer, so an */
            "static %s*tycho_arr_C%d_ptr(TychoArrC%d *xs, tycho_int i) {\n"   /* element is a mutable lvalue */
            "    if (i < 0 || i >= xs->len) { fprintf(stderr, \"tycho: index %%\" TY_PRId \" out of bounds (len %%\" TY_PRId \")\\n\", i, xs->len); exit(1); }\n"
            "    return &xs->data[i];\n}\n", ct, i, i);
        fprintf(o,
            "static void tycho_arr_C%d_set(Arena *a, TychoArrC%d *xs, tycho_int i, %sv) {\n"
            "    if (i < 0 || i >= xs->len) { fprintf(stderr, \"tycho: index %%\" TY_PRId \" out of bounds (len %%\" TY_PRId \")\\n\", i, xs->len); exit(1); }\n"
            "    xs->data[i] = %s;\n}\n", i, i, ct, copy_into(et, "a", "v"));
        fprintf(o,
            "static TychoArrC%d tycho_arr_C%d_copy(Arena *a, TychoArrC%d src) {\n"
            "    TychoArrC%d r = tycho_arr_C%d_with_cap(a, src.len); r.len = src.len;\n"
            "    for (tycho_int i = 0; i < src.len; i++) r.data[i] = %s;\n"
            "    return r;\n}\n", i, i, i, i, i, copy_into(et, "a", "src.data[i]"));
        fprintf(o,
            "static int tycho_arr_C%d_eq(TychoArrC%d x, TychoArrC%d y) {\n"
            "    if (x.len != y.len) return 0;\n"
            "    for (tycho_int i = 0; i < x.len; i++) if (!(%s)) return 0;\n"
            "    return 1;\n}\n\n", i, i, i, gen_eq(et, "x.data[i]", "y.data[i]"));
        if (struct_keyused(T_ARRC_BASE + i) || hash_keyused(T_ARRC_BASE + i))   /* composite-element array: map key or hash(x): order-sensitive deep hash */
            fprintf(o,
                "static uint64_t tycho_arr_C%d_hash(TychoArrC%d x) {\n"
                "    uint64_t h = tycho_hash_k0;\n"
                "    for (tycho_int i = 0; i < x.len; i++) h = h * UINT64_C(1099511628211) ^ %s;\n"
                "    return h;\n}\n\n", i, i, gen_hash(et, "x.data[i]"));
        if (hash_keyused(T_ARRC_BASE + i)) {   /* hash(x): the deterministic twin */
            g_hash_det = 1;
            fprintf(o,
                "static uint64_t tycho_arr_C%d_dhash(TychoArrC%d x) {\n"
                "    uint64_t h = UINT64_C(0x9e3779b97f4a7c15);\n"
                "    for (tycho_int i = 0; i < x.len; i++) h = h * UINT64_C(1099511628211) ^ %s;\n"
                "    return h;\n}\n\n", i, i, gen_hash(et, "x.data[i]"));
            g_hash_det = 0;
        }
    }
    /* (7a') composite-map ops [string: V] — a parameterized copy of the embedded
     * TychoMapSI (open addressing, NULL-empty slots + backward-shift delete, keyed
     * SipHash string keys), with the VALUE
     * generalized to any type: put deep-copies the value into the map's arena via
     * copy_into, exactly like a composite-array element. Emitted after struct/array
     * bodies so a struct/array value type's copy fn is already available. */
    for (int i = 0; i < g_nmaptypes; i++) {
        if (has_typaram(T_MAPC_BASE + i)) continue;   /* generics: a `[$K: $V]` template map -- transient, never emitted */
        Type keyt = g_maptypes[i].key;
        const char *ct = c_type(g_maptypes[i].val);   /* value C type -- stored inline in the dense entries (the [int:Trie] win) */
        const char *ks = mapc_kslot(keyt);
        char *kp = mapc_kparam(keyt);
        char *kpo = mapc_kparam_own(keyt);   /* _append takes ownership -- see mapc_kparam_own */
        char *kcopy = mapc_kcopy(keyt);
        char *vcopy = copy_into(g_maptypes[i].val, "a", "v");
        Type kat = arr_of(keyt); const char *katc = c_type(kat), *katf = arr_fn(kat);
        char *kelem = mapc_keyelem(keyt);
        /* --- compact indexed-dict family (mirrors runtime tycho_map_ii): int32 index
         * table -> dense insertion-ordered entries; tombstone + backward-shift-index
         * delete + allocation-free in-place compaction (churn bound). --- */
        fprintf(o,
            "static TychoMapC%d tycho_mapc%d_with_cap(Arena *a, tycho_int cap) {\n"
            "    TychoMapC%d m; m.len = 0; m.ecount = 0;\n"
            "    if (cap <= 0) { m.ekeys = 0; m.evals = 0; m.elive = 0; m.idx = 0; m.ecap = 0; m.icap = 0; return m; }\n"
            "    tycho_int ec = 4; while (ec < cap) ec *= 2; tycho_int ic = 8; while (ic < cap * 2) ic *= 2; m.ecap = ec; m.icap = ic;\n"
            "    m.ekeys = (%s*)arena_alloc(a, (size_t)ec * sizeof(%s));\n"
            "    m.evals = (%s*)arena_alloc(a, (size_t)ec * sizeof(%s));\n"
            "    m.elive = (unsigned char *)arena_alloc(a, (size_t)ec);\n"
            "    m.idx = (int *)arena_alloc(a, (size_t)ic * sizeof(int));\n"
            "    for (tycho_int i = 0; i < ic; i++) m.idx[i] = 0; return m;\n}\n", i, i, i, ks, ks, ct, ct);
        fprintf(o,
            "static void tycho_mapc%d_reserve(Arena *a, TychoMapC%d *m, tycho_int cap) {\n"
            "    tycho_int ec = 4; while (ec < cap) ec *= 2; tycho_int ic = 8; while (ic < cap * 2) ic *= 2;\n"
            "    if (cap <= 0) return;\n"
            "    if (m->ecap >= ec && m->icap >= ic) return;\n"
            "    %s *nk = (%s *)arena_alloc(a, (size_t)ec * sizeof(%s));\n"
            "    %s *nv = (%s *)arena_alloc(a, (size_t)ec * sizeof(%s));\n"
            "    unsigned char *nl = (unsigned char *)arena_alloc(a, (size_t)ec);\n"
            "    int *ni = (int *)arena_alloc(a, (size_t)ic * sizeof(int));\n"
            "    for (tycho_int i = 0; i < ic; i++) ni[i] = 0;\n"
            "    for (tycho_int e = 0; e < m->ecount; e++) { nk[e] = m->ekeys[e]; nv[e] = m->evals[e]; nl[e] = m->elive[e]; }\n"
            "    m->ekeys = nk; m->evals = nv; m->elive = nl; m->ecap = ec;\n"
            "    m->idx = ni; m->icap = ic;\n"
            "    for (tycho_int e = 0; e < m->ecount; e++) if (m->elive[e]) tycho_mapc%d_idxput(m, e);\n}\n", i, i, ks, ks, ks, ct, ct, ct, i);
        fprintf(o,
            "static tycho_int tycho_mapc%d_find(TychoMapC%d m, %s) {\n"
            "    if (m.icap == 0) return -1; uint64_t mask = (uint64_t)m.icap - 1; tycho_int i = (tycho_int)(%s & mask); int e;\n"
            "    while ((e = m.idx[i]) != 0) { if (%s) return e - 1; i = (tycho_int)((i + 1) & mask); }\n"
            "    return -1;\n}\n", i, i, kp, mapc_khash(keyt, "k"), mapc_kmatch(keyt, "m.ekeys[e - 1]", "k"));
        fprintf(o,
            "static void tycho_mapc%d_idxput(TychoMapC%d *m, tycho_int ei) {\n"
            "    uint64_t mask = (uint64_t)m->icap - 1; tycho_int i = (tycho_int)(%s & mask);\n"
            "    while (m->idx[i] != 0) i = (tycho_int)((i + 1) & mask); m->idx[i] = (int)(ei + 1);\n}\n",
            i, i, mapc_khash(keyt, "m->ekeys[ei]"));
        fprintf(o,
            "static void tycho_mapc%d_idxgrow(Arena *a, TychoMapC%d *m) {\n"
            "    tycho_int ic = m->icap ? m->icap * 2 : 8; int *ni = (int *)arena_alloc(a, (size_t)ic * sizeof(int));\n"
            "    for (tycho_int i = 0; i < ic; i++) ni[i] = 0; m->idx = ni; m->icap = ic;\n"
            "    for (tycho_int e = 0; e < m->ecount; e++) if (m->elive[e]) tycho_mapc%d_idxput(m, e);\n}\n", i, i, i);
        fprintf(o,
            "static void tycho_mapc%d_compact(TychoMapC%d *m) {\n"
            "    tycho_int w = 0; for (tycho_int r = 0; r < m->ecount; r++) if (m->elive[r]) { if (w != r) { m->ekeys[w] = m->ekeys[r]; m->evals[w] = m->evals[r]; m->elive[w] = 1; } w++; }\n"
            "    m->ecount = w; for (tycho_int i = 0; i < m->icap; i++) m->idx[i] = 0;\n"
            "    for (tycho_int e = 0; e < m->ecount; e++) tycho_mapc%d_idxput(m, e);\n}\n", i, i, i);
        fprintf(o,
            "static tycho_int tycho_mapc%d_append(Arena *a, TychoMapC%d *m, %s, %sv) {\n"
            "    if (m->ecount == m->ecap) { tycho_int dead = m->ecount - m->len;\n"
            "        if (dead > m->ecap / 2) tycho_mapc%d_compact(m);\n"
            "        else { tycho_int nc = m->ecap ? m->ecap * 2 : 4;\n"
            "            %s*nk = (%s*)arena_alloc(a, (size_t)nc * sizeof(%s)); %s*nv = (%s*)arena_alloc(a, (size_t)nc * sizeof(%s)); unsigned char *nl = (unsigned char *)arena_alloc(a, (size_t)nc);\n"
            "            for (tycho_int e = 0; e < m->ecount; e++) { nk[e] = m->ekeys[e]; nv[e] = m->evals[e]; nl[e] = m->elive[e]; }\n"
            "            m->ekeys = nk; m->evals = nv; m->elive = nl; m->ecap = nc; } }\n"
            "    tycho_int e = m->ecount++; m->ekeys[e] = k; m->evals[e] = v; m->elive[e] = 1; return e;\n}\n",
            i, i, kpo, ct, i, ks, ks, ks, ct, ct, ct);
        fprintf(o,
            "static void tycho_mapc%d_put(Arena *a, TychoMapC%d *m, %s, %sv) {\n"
            "    tycho_int e = tycho_mapc%d_find(*m, k); if (e >= 0) { m->evals[e] = %s; return; }\n"
            "    if ((m->len + 1) * 2 > m->icap) tycho_mapc%d_idxgrow(a, m);\n"
            "    tycho_int ne = tycho_mapc%d_append(a, m, %s, %s); m->len++; tycho_mapc%d_idxput(m, ne);\n}\n",
            i, i, kp, ct, i, vcopy, i, i, kcopy, vcopy, i);
        fprintf(o,
            "static inline %s*tycho_mapc%d_slotptr(Arena *a, TychoMapC%d *m, %s) {\n"
            "    tycho_int e = tycho_mapc%d_find(*m, k); if (e >= 0) return &m->evals[e];\n"
            "    if ((m->len + 1) * 2 > m->icap) tycho_mapc%d_idxgrow(a, m);\n"
            "    tycho_int ne = tycho_mapc%d_append(a, m, %s, (%s){0}); m->len++; tycho_mapc%d_idxput(m, ne);\n"
            "    return &m->evals[ne];\n}\n", ct, i, i, kp, i, i, i, kcopy, ct, i);
        fprintf(o,
            "static void tycho_mapc%d_del(TychoMapC%d *m, %s) {\n"
            "    if (m->icap == 0) return; uint64_t mask = (uint64_t)m->icap - 1; tycho_int i = (tycho_int)(%s & mask), found = -1;\n"
            "    while (m->idx[i] != 0) { if (%s) { found = i; break; } i = (tycho_int)((i + 1) & mask); }\n"
            "    if (found < 0) return; tycho_int ei = m->idx[found] - 1; m->elive[ei] = 0; m->len--;\n"
            "    tycho_int g = found;\n"
            "    for (;;) { m->idx[g] = 0; tycho_int j = g;\n"
            "        for (;;) { j = (tycho_int)((j + 1) & mask); if (m->idx[j] == 0) return;\n"
            "            tycho_int h = (tycho_int)(%s & mask);\n"
            "            if (g <= j) { if (g < h && h <= j) continue; } else { if (g < h || h <= j) continue; } break; }\n"
            "        m->idx[g] = m->idx[j]; g = j; }\n}\n",
            i, i, kp, mapc_khash(keyt, "k"), mapc_kmatch(keyt, "m->ekeys[m->idx[i] - 1]", "k"), mapc_khash(keyt, "m->ekeys[m->idx[j] - 1]"));
        fprintf(o,
            "static TychoMapC%d tycho_mapc%d_copy(Arena *a, TychoMapC%d src) {\n"
            "    TychoMapC%d r = tycho_mapc%d_with_cap(a, src.len ? src.len : 0);\n"
            "    for (tycho_int e = 0; e < src.ecount; e++) if (src.elive[e]) tycho_mapc%d_put(a, &r, src.ekeys[e], src.evals[e]);\n"
            "    return r;\n}\n", i, i, i, i, i, i);
        fprintf(o,
            "static TychoMapC%d tycho_mapc%d_set(Arena *a, TychoMapC%d m, %s, %sv) {\n"
            "    TychoMapC%d r = tycho_mapc%d_copy(a, m); tycho_mapc%d_put(a, &r, k, v); return r;\n}\n", i, i, i, kp, ct, i, i, i);
        fprintf(o,
            "static TychoMapC%d tycho_mapc%d_del_pure(Arena *a, TychoMapC%d m, %s) {\n"
            "    TychoMapC%d r = tycho_mapc%d_copy(a, m); tycho_mapc%d_del(&r, k); return r;\n}\n", i, i, i, kp, i, i, i);
        fprintf(o,
            "static %stycho_mapc%d_get(TychoMapC%d m, %s, %sdflt) {\n"
            "    tycho_int e = tycho_mapc%d_find(m, k); return e < 0 ? dflt : m.evals[e];\n}\n", ct, i, i, kp, ct, i);
        fprintf(o,
            "static int tycho_mapc%d_has(TychoMapC%d m, %s) { return tycho_mapc%d_find(m, k) >= 0; }\n", i, i, kp, i);
        fprintf(o,
            "static %stycho_mapc%d_keys(Arena *a, TychoMapC%d m) {\n"
            "    %sr = tycho_arr_%s_with_cap(a, m.len);\n"
            "    for (tycho_int e = 0; e < m.ecount; e++) if (m.elive[e]) tycho_arr_%s_push(a, &r, %s);\n"
            "    return r;\n}\n", katc, i, i, katc, katf, katf, kelem);
        fprintf(o,
            "static int tycho_mapc%d_eq(TychoMapC%d x, TychoMapC%d y) {\n"
            "    if (x.len != y.len) return 0;\n"
            "    for (tycho_int e = 0; e < x.ecount; e++) if (x.elive[e]) { tycho_int s = tycho_mapc%d_find(y, x.ekeys[e]); if (s < 0 || !(%s)) return 0; }\n"
            "    return 1;\n}\n\n", i, i, i, i, gen_eq(g_maptypes[i].val, "y.evals[s]", "x.evals[e]"));
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
        fprintf(o, "    if (s->len == s->cap) {\n        tycho_int nc = s->cap ? s->cap * 2 : 4;\n");
        for (int f = 0; f < sd->nfields; f++) {
            const char *ct = c_type(sd->fields[f].type);
            fprintf(o, "        %s*n%d = (%s*)arena_alloc(a, (size_t)nc * sizeof(%s)); "
                       "for (tycho_int i = 0; i < s->len; i++) n%d[i] = s->f%d[i]; s->f%d = n%d;\n",
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
            fprintf(o, "    for (tycho_int i = 0; i < s.len; i++) r.f%d[i] = %s;\n", f,
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
        fprintf(o, "    for (tycho_int i = 0; i < a.len; i++) if (!(%s)) return 0;\n", conj);
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
    /* (9b) str() bodies (F5): render each aggregate to a Tycho string. Emitted for
     * every non-generic type (like eq), forward-declared above, so recursion and
     * mutual references resolve. Each folds tycho_str_concat; gen_str recurses for
     * fields/elements. Format: Point(1, 2) · [1, 2, 3] · [a: 1] · Some(3). */
    for (int i = 0; i < g_nstructs; i++) {
        if (g_structs[i].generic) continue;
        StructDef *sd = &g_structs[i];
        fprintf(o, "static char *tycho_str_S_%s(Arena *a, S_%s v) {\n", sd->name, sd->name);
        fprintf(o, "    char *r = tycho_str_from_c(a, \"%s(\");\n", sd->name);
        for (int j = 0; j < sd->nfields; j++) {
            if (j) fprintf(o, "    r = tycho_str_concat(a, r, tycho_str_from_c(a, \", \"));\n");
            fprintf(o, "    r = tycho_str_concat(a, r, %s);\n",
                    gen_str(sd->fields[j].type, "a", sfmt("v.f_%s", sd->fields[j].name)));
        }
        fprintf(o, "    return tycho_str_concat(a, r, tycho_str_from_c(a, \")\"));\n}\n");
    }
    for (int i = 0; i < g_nenums; i++) {
        if (g_enums[i].generic) continue;
        EnumDef *ed = &g_enums[i];
        fprintf(o, "static char *tycho_str_E_%s(Arena *a, E_%s *v) {\n", ed->name, ed->name);
        for (int v2 = 0; v2 < ed->nvariants; v2++) {
            Variant *var = &ed->variants[v2];
            if (var->npayload == 0) {
                fprintf(o, "    if (v->tag == %d) return tycho_str_from_c(a, \"%s\");\n", v2, var->name);
            } else {
                fprintf(o, "    if (v->tag == %d) {\n", v2);
                fprintf(o, "        char *r = tycho_str_from_c(a, \"%s(\");\n", var->name);
                for (int f = 0; f < var->npayload; f++) {
                    if (f) fprintf(o, "        r = tycho_str_concat(a, r, tycho_str_from_c(a, \", \"));\n");
                    fprintf(o, "        r = tycho_str_concat(a, r, %s);\n",
                            gen_str(var->payload[f], "a", sfmt("v->u.%s.f%d", var->name, f)));
                }
                fprintf(o, "        return tycho_str_concat(a, r, tycho_str_from_c(a, \")\"));\n    }\n");
            }
        }
        fprintf(o, "    return tycho_str_from_c(a, \"<?>\");\n}\n");   /* unreachable: tag always matches a variant */
    }
    for (int i = 0; i < g_narrtypes; i++) {
        if (has_typaram(T_ARRC_BASE + i)) continue;
        Type et = g_arrtypes[i].elem;
        fprintf(o, "static char *tycho_str_arr_C%d(Arena *a, TychoArrC%d xs) {\n", i, i);
        fprintf(o, "    char *r = tycho_str_from_c(a, \"[\");\n");
        if (g_arrtypes[i].bnd) {   /* bounded[N]T: inline storage xs.v[i], runtime count xs.len */
            fprintf(o, "    for (tycho_int i = 0; i < xs.len; i++) {\n");
            fprintf(o, "        if (i) r = tycho_str_concat(a, r, tycho_str_from_c(a, \", \"));\n");
            fprintf(o, "        r = tycho_str_concat(a, r, %s);\n", gen_str(et, "a", "xs.v[i]"));
        } else if (g_arrtypes[i].size > 0) {   /* fixed [N]T: inline storage xs.v[i] */
            fprintf(o, "    for (tycho_int i = 0; i < %lld; i++) {\n", (long long)g_arrtypes[i].size);
            fprintf(o, "        if (i) r = tycho_str_concat(a, r, tycho_str_from_c(a, \", \"));\n");
            fprintf(o, "        r = tycho_str_concat(a, r, %s);\n", gen_str(et, "a", "xs.v[i]"));
        } else {
            fprintf(o, "    for (tycho_int i = 0; i < xs.len; i++) {\n");
            fprintf(o, "        if (i) r = tycho_str_concat(a, r, tycho_str_from_c(a, \", \"));\n");
            fprintf(o, "        r = tycho_str_concat(a, r, %s);\n", gen_str(et, "a", "xs.data[i]"));
        }
        fprintf(o, "    }\n    return tycho_str_concat(a, r, tycho_str_from_c(a, \"]\"));\n}\n");
    }
    for (int i = 0; i < g_nmaptypes; i++) {
        if (has_typaram(T_MAPC_BASE + i)) continue;
        Type kt = g_maptypes[i].key, vt = g_maptypes[i].val;
        Type kb = base_of(kt);   /* fieldless-enum key is stored as a tag: rebuild the value via the singleton table */
        char *kexpr = IS_ENUM(kb) ? sfmt("_sing_tab_%s[m.ekeys[e]]", g_enums[ENUM_ID(kb)].name)
                                  : sfmt("m.ekeys[e]");
        fprintf(o, "static char *tycho_str_mapc%d(Arena *a, TychoMapC%d m) {\n", i, i);
        fprintf(o, "    char *r = tycho_str_from_c(a, \"[\"); int first = 1;\n");
        fprintf(o, "    for (tycho_int e = 0; e < m.ecount; e++) if (m.elive[e]) {\n");
        fprintf(o, "        if (!first) r = tycho_str_concat(a, r, tycho_str_from_c(a, \", \")); first = 0;\n");
        fprintf(o, "        r = tycho_str_concat(a, r, %s);\n", gen_str(kt, "a", kexpr));
        fprintf(o, "        r = tycho_str_concat(a, r, tycho_str_from_c(a, \": \"));\n");
        fprintf(o, "        r = tycho_str_concat(a, r, %s);\n", gen_str(vt, "a", "m.evals[e]"));
        fprintf(o, "    }\n    return tycho_str_concat(a, r, tycho_str_from_c(a, \"]\"));\n}\n");
    }
    fputs("\n", o);
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

    /* lex every file once; collect this package's imports from the headers.
     * Keep each file's source text (srcs[i]) so the parse loop below can point
     * g_src at the RIGHT file for die_at's snippet -- otherwise g_src is left at
     * whatever lex saw last (a corelib file), and a parse error in this file
     * prints the correct name:line but a snippet from the wrong source. */
    TokVec toks[512];
    char *srcs[512];
    char *imp_paths[256]; int n_imp = 0;
    for (int i = 0; i < nf; i++) {
        g_srcname = files[i];
        char *s = read_file(files[i]);
        srcs[i] = s;
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
        g_src = srcs[i];                 /* snippet from THIS file, not the last one lexed */
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
            pv.v[j]->srcfile = files[i];   /* so resolve/codegen diagnostics name THIS file, not the last-parsed one */
            pv.v[j]->srctext = srcs[i];
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
    int print_shims = 0;   /* --print-shims: list the transitive <pkg>_shim.c closure, no codegen */
    int print_deps  = 0;   /* --print-deps: list the transitive pkg-config names from <pkg>/deps, no codegen */
    int bundle = 0;
    int debug = 0;    /* -g: emit #line directives + build with -O0 -g (single-file only) */
    int native = 0;   /* --native: add -march=native (non-portable: SIGILL on a different CPU) */
    char *extra = sfmt("%s", "");   /* FFI: extra cc link/include flags (-L/-I/--link/--pkg) */
    char *shims = sfmt("%s", "");   /* FFI: companion C shim sources (--shim) compiled+linked alongside */

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--emit-c")) emit_c_only = 1;
        else if (!strcmp(argv[i], "--symbols")) want_symbols = 1;
        else if (!strcmp(argv[i], "--print-shims")) print_shims = 1;
        else if (!strcmp(argv[i], "--print-deps")) { print_deps = 1; g_pkgdeps_names_only = 1; }
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
                        "                     [-L<dir>] [-I<dir>] [--link <lib>] [--shim <file.c>] [--pkg <name>]\n"
                        "                     [--print-shims] [--print-deps]\n"
                        "  --emit-c with -o writes <name>.c; with no -o it writes the C to stdout.\n"
                        "  --print-shims lists the <pkg>_shim.c files this program needs, one per line,\n"
                        "  transitively -- what a lane linking the emitted C by hand must pass to cc.\n"
                        "  --print-deps lists the pkg-config package names this program needs, one per\n"
                        "  line, transitively -- what a lane must probe to decide whether to SKIP.\n");
        return 1;
    }
    g_srcname = input;

    if (bundle) {   /* emit the package's source as one post-order stream (for tychoc0) */
        bundle_pkg(path_dir(input), 1);
        return 0;
    }

    char *base   = out ? xstrndup(out, strlen(out)) : strip_ext(input);
    char *c_path = sfmt("%s.c", base);
    /* `--emit-c` with no `-o` writes the C to STDOUT. HISTORY: it used to write
     * the sibling `<src>.c` and print `wrote <path>` on stdout, so the obvious
     * `tychoc x.ty --emit-c > out.c` captured the status line and left an
     * untracked x.c in the tree. The .gitignore route was considered and
     * rejected: its emitted-C rules are a hand-listed set of specific paths
     * (the compiler dir, tychoc0.c, tychofmt.c, ...), and 31 directories hold
     * BOTH .ty sources and hand-written .c shims (the corelib, bench and tools
     * dirs, tests/ffi), so a by-pattern ignore would hide a newly added shim --
     * a worse failure than the one it fixes. See the loops-cleanup plan. All but one
     * in-tree caller already passed -o (the fuzz runners,
     * scripts/entrypoints.sh:63, bench/guard.sh:28, the three examples
     * sanitizer legs); the exception was the bytes-rehome lane in
     * scripts/tools_check.sh:283, which greps the sibling file, and it was
     * given an explicit -o here. */
    int c_to_stdout = emit_c_only && !out;

    char *src = read_file(input);
    TokVec toks = lex(src);
    const char *pkg = detect_package(toks.v);
    ProcVec prog = pkg ? compile_package(input, pkg)   /* package: merge the whole directory */
                       : parse_program(toks.v);        /* single file: unchanged */
    /* F8: a bare (package-less) file parses its `import`s but the single-file path
     * never loads them, so `pkg.symbol` would fail later with a misleading "package
     * has no symbol". Point at the real fix — every file that imports must name its
     * own package (Odin-style), e.g. `package main`. */
    if (!pkg && g_nimports > 0) {
        fprintf(stderr, "%s:%d: error: to `import` a package, this file must declare its own package first -- add `package main` (or another name) as the first line\n",
                input, g_imports[0].line);
        return 1;
    }
    /* --print-shims: the companion-shim closure this program needs, one path per
     * line and nothing else on stdout, so a shell can splice it straight onto a cc
     * line. It does not compute anything new -- `compile_package` above has already
     * walked the whole import graph, and `merge_pkg` calls `add_shim` as the DFS
     * *enters* each package, so a shim reached only through an indirect import is
     * in `g_shims` on exactly the same terms as a directly imported one. That
     * closure is what the normal build path splices onto its cc line; `--emit-c`
     * returns before ever reaching it, which is why every hand-linked lane had to
     * guess. Two lanes guessed from direct imports and went stale twice on
     * transitive deps (`examples/fetch/run.sh`'s note records both). They ask here
     * now.
     *
     * Printed BEFORE `check_finite_types`/`resolve_program`, deliberately: the
     * shim list is a property of the import graph, not of the program type-checking,
     * and a build script wants the link line even while the program is mid-edit.
     * A single-file program with no package prints nothing and exits 0 -- an empty
     * closure is a correct answer, not a failure. */
    if (print_shims) {
        for (int i = 0; i < g_nshims; i++) printf("%s\n", g_shims[i]);
        return 0;
    }
    /* --print-deps: the same closure, read out as pkg-config NAMES instead of shim
     * paths. It exists because `--print-shims` cannot answer the question the test
     * harnesses actually ask. They do not link the emitted C -- they let tychoc
     * build -- so they need no shim list at all; what they need is "would this
     * program's link line resolve on this host?", so that a missing library is a
     * SKIP rather than a red. That question is about names, and until this flag the
     * only way to get them was to grep the program's own `import` lines and map
     * each to `corelib/<mod>/deps` -- which sees direct imports only, exactly the
     * transitive blindness `--print-shims` was added to fix on the other two lanes.
     *
     * `g_pkgnames` is filled by the same `merge_pkg` DFS that fills `g_shims`
     * (`add_pkg_deps` beside `add_shim` as the walk enters a package), so an
     * indirect import's dependency is listed on the same terms as a direct one.
     * Names, not the resolved `g_pkgdeps` flags: on the host where the library is
     * missing the flags are empty, which is the one host whose answer matters.
     * Printed here, before type checking, for the reason `--print-shims` is. */
    if (print_deps) {
        for (int i = 0; i < g_npkgnames; i++) printf("%s\n", g_pkgnames[i]);
        return 0;
    }
    if (debug && !pkg) {   /* -g: line info only for single-file builds -- merged packages lose per-node filenames */
        g_line_info = 1;
        g_line_file = c_escape_path(input);
    } else if (debug && pkg) {
        fprintf(stderr, "tychoc: -g line info is emitted only for single-file compiles; skipped for this package build\n");
    }
    check_finite_types();   /* reject by-value-recursive types before the resolver */
    resolve_program(&prog);

    if (want_symbols) { emit_symbols(&prog); return 0; }   /* LSP index; no codegen */

    FILE *o = c_to_stdout ? stdout : fopen(c_path, "wb");
    if (!o) { fprintf(stderr, "tychoc: cannot write %s\n", c_path); return 1; }
    gen_program(o, &prog);
    if (c_to_stdout) fflush(o); else fclose(o);

    if (emit_c_only) {
        if (!c_to_stdout) printf("wrote %s\n", c_path);
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
    if (rc != 0) { fprintf(stderr, "tychoc: C compilation failed (%s)\n", cmd); return 1; }   /* the .c SURVIVES a cc failure on purpose: it is the evidence the printed command refers to */
    /* The generated C is an INTERMEDIATE, not an artifact. It used to be left beside the
     * source on every plain build (`tychoc tests/for3.ty` -> an untracked `tests/for3.c`),
     * which no .gitignore rule can safely cover: 31 directories hold both .ty sources and
     * hand-written, tracked .c files, and 27 of those .c files share a basename with the
     * sibling .ty (all of bench/, the hand-written C ports README:29 builds against), so a
     * by-pattern ignore would hide a real file. Removing it here is the same fix `--emit-c`
     * with no -o got in the loops-cleanup plan; this is phase 52. To KEEP the C, ask for it:
     * `--emit-c -o name` (docs/guides/debugging.md:37 is the workflow that does).
     * Note the pre-existing hazard this does NOT introduce: a plain build of `bench/json/json.ty`
     * already OVERWROTE the hand-written `bench/json/json.c` before it got here. Nothing in
     * the tree does that -- every in-tree build passes -o or --emit-c -o. */
    remove(c_path);
    printf("built %s\n", base);
    return 0;
}
