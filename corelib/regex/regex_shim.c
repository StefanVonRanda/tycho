/* core:regex C shim -- POSIX extended regular expressions over <regex.h> (libc,
 * no extra -l). Auto-compiled+linked when a program imports "core:regex" (the
 * compiler discovers <pkg>/<pkg>_shim.c). A compiled pattern is a malloc'd
 * regex_t* handed to hier as an opaque `ptr`; hier never dereferences it and
 * frees it via rx_free (FFI memory is NOT arena-managed). */
#include <regex.h>
#include <stdlib.h>

void *rx_compile(const char *pattern) {        /* compile ERE; NULL on a bad pattern */
    regex_t *re = (regex_t *)malloc(sizeof(regex_t));
    if (!re) return NULL;
    if (regcomp(re, pattern, REG_EXTENDED) != 0) { free(re); return NULL; }
    return re;
}

long rx_is_match(void *re, const char *s) {    /* 1 if re matches anywhere in s, else 0 */
    if (!re) return 0;
    return regexec((regex_t *)re, s, 0, NULL, 0) == 0 ? 1 : 0;
}

long rx_find(void *re, const char *s) {        /* byte offset of the first match, or -1 */
    regmatch_t m;
    if (!re || regexec((regex_t *)re, s, 1, &m, 0) != 0) return -1;
    return (long)m.rm_so;
}

long rx_find_end(void *re, const char *s) {    /* one-past-end offset of the first match, or -1 */
    regmatch_t m;
    if (!re || regexec((regex_t *)re, s, 1, &m, 0) != 0) return -1;
    return (long)m.rm_eo;
}

void rx_free(void *re) {                        /* free a compiled pattern */
    if (re) { regfree((regex_t *)re); free(re); }
}
