/* Fuzzer FFI shim: a fixed vocabulary of extern functions the generator calls,
 * linked into every fuzz binary by run.py. Lets the differential + ASan/UBSan
 * oracle stress the extern / ptr / string-return-arena-copy codegen of BOTH
 * compilers. Signatures must match the `extern fn` decls emitted by gen.py. */
#include <string.h>

static char fz_slot;

long        fz_addi(long a, long b)    { return a + b; }
double      fz_addf(double a, double b){ return a + b; }
const char *fz_echo(const char *s)     { return s; }                          /* str arg + str return (tycho arena-copies) */
const char *fz_nullify(const char *s)  { return (s && *s) ? s : 0; }          /* NULL return -> tycho guards to "" */
long        fz_slen(const char *s)     { return (long)strlen(s); }            /* str arg -> int */
void       *fz_handle(long id)         { return id > 0 ? (void *)&fz_slot : 0; } /* int -> ptr (NULL for id<=0) */
long        fz_unwrap(void *h)         { return h ? 1 : 0; }                   /* ptr arg -> int */
