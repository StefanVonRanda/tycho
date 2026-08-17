#include "os_shim.c"
#include <shellapi.h>
#include <stdio.h>

static int check(const char *name, const char *const *args, int n) {
    /* the vector is the borrowed (ptr,len) pair os.exec now hands the shim */
    char *line = osx_win_cmdline(args, (tycho_int)n);
    if (!line) { printf("FAIL %s (cmdline)\n", name); return 1; }

    wchar_t wline[8192];
    MultiByteToWideChar(CP_UTF8, 0, line, -1, wline, 8192);
    int wn = 0;
    wchar_t **wargv = CommandLineToArgvW(wline, &wn);
    if (!wargv) { printf("FAIL %s (CommandLineToArgvW)\n", name); free(line); return 1; }

    int bad = 0;
    if (wn != n) { printf("FAIL %s: split into %d args, expected %d\n", name, wn, n); bad = 1; }
    for (int i = 0; i < wn && i < n && !bad; i++) {
        char got[4096];
        WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, got, sizeof got, NULL, NULL);
        if (strcmp(got, args[i]) != 0) {
            printf("FAIL %s: arg %d is [%s], expected [%s]\n", name, i, got, args[i]);
            bad = 1;
        }
    }
    if (!bad) printf("ok   %-22s line=%s\n", name, line);
    LocalFree(wargv);
    free(line);
    return bad;
}

int main(void) {
    int bad = 0;
    /* argv[0] stays a plain name in every case: CommandLineToArgvW applies
     * DIFFERENT rules to argv[0] than to the rest, so quoting it is a separate
     * question from the one under test here. */
    const char *simple[]  = { "prog", "a", "b" };
    const char *spaces[]  = { "prog", "a b c", "tail" };
    const char *quotes[]  = { "prog", "say \"hi\"", "tail" };
    const char *bslash[]  = { "prog", "C:\\dir\\", "tail" };
    const char *bsquote[] = { "prog", "a\\\"b", "tail" };
    const char *empty[]   = { "prog", "", "tail" };
    const char *meta[]    = { "prog", "; rm -rf / & echo x | y > z", "tail" };
    const char *tabnl[]   = { "prog", "a\tb", "tail" };
    const char *manybs[]  = { "prog", "\\\\\\", "tail" };

    bad |= check("simple",         simple,  3);
    bad |= check("spaces",         spaces,  3);
    bad |= check("embedded-quote", quotes,  3);
    bad |= check("trailing-bslash",bslash,  3);
    bad |= check("bslash-quote",   bsquote, 3);
    bad |= check("empty-arg",      empty,   3);
    bad |= check("metacharacters", meta,    3);
    bad |= check("tab",            tabnl,   3);
    bad |= check("backslash-run",  manybs,  3);

    printf(bad ? "quotecheck: FAIL\n" : "quotecheck: all round-trip\n");
    return bad;
}
