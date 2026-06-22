/* Companion C for tools/lsp.ty (the tycho language server), linked via
 * `tychoc --shim`. LSP speaks JSON-RPC over stdin/stdout with Content-Length
 * framing, which needs INCREMENTAL stdin reads (a header line, then exactly N
 * body bytes) and a FLUSHED stdout after every message -- neither of which
 * tycho's read_all()/print() can do. These wrap stdio through our own symbols
 * (no clash with the preamble's <stdio.h> decls). Signatures match tycho's extern
 * emission: tycho `int` == C `long`, tycho `string` == `char*`; a returned char*
 * is arena-copied into a tycho string by the FFI, so static/realloc buffers are
 * safe (copied before the next call). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Read one line (through the newline) from stdin; "" on EOF. */
char *tycho_read_line(void) {
    static char buf[65536];
    if (!fgets(buf, sizeof buf, stdin)) return "";
    return buf;
}

/* Read EXACTLY n bytes from stdin (looping over short reads); the JSON body. */
char *tycho_read_n(long n) {
    static char *buf = NULL;
    static long cap = 0;
    if (n + 1 > cap) {
        char *nb = realloc(buf, (size_t)(n + 1));
        if (!nb) return "";
        buf = nb;
        cap = n + 1;
    }
    long got = 0;
    while (got < n) {
        size_t r = fread(buf + got, 1, (size_t)(n - got), stdin);
        if (r == 0) break;                 /* EOF */
        got += (long)r;
    }
    buf[got] = 0;
    return buf;
}

/* Write a string to stdout and flush (so the client sees the reply immediately). */
long tycho_write(char *s) {
    size_t len = strlen(s);
    fwrite(s, 1, len, stdout);
    fflush(stdout);
    return 0;
}

/* Run a command (to invoke the compiler on a buffer for diagnostics). */
long tycho_run(char *cmd) { return (long)system(cmd); }
