/* Round-trip check for the Windows argv quoter in os_shim.c -- WINDOWS ONLY.
 *
 * THE CLAIM UNDER TEST. osx_win_cmdline(argv) produces a command line that
 * CommandLineToArgvW -- the splitter Windows programs actually use -- splits
 * back into the SAME vector. If that holds, no argument can become two and no
 * metacharacter can become syntax, which is the whole security property of
 * os.exec on Windows.
 *
 * WHY THIS EXISTS SEPARATELY from corelib/test/os. That test asserts the
 * round trip on POSIX by handing a hostile argument to `printf` and reading it
 * back verbatim. Windows guarantees no program that echoes argv WITHOUT
 * re-parsing it (cmd.exe`s own `echo` re-parses it), so the assertion has to be made
 * against the splitter directly, in C. Run by scripts/shim_check.sh on Windows.
 *
 * It #includes the .c so the statics are reachable. Not a shim: nothing links
 * it into a program, and tychoc only auto-discovers <mod>_shim.c.
 */
#include "os_shim.c"
#include <shellapi.h>
#include <stdio.h>

static int check(const char *name, const char **args, int n) {
    void *h = osx_argv_new();
    if (!h) { printf("FAIL %s (argv_new)\n", name); return 1; }
    for (int i = 0; i < n; i++)
        if (!osx_argv_push(h, args[i])) { printf("FAIL %s (push %d)\n", name, i); osx_argv_free(h); return 1; }

    char *line = osx_win_cmdline((OsArgv *)h);
    if (!line) { printf("FAIL %s (cmdline)\n", name); osx_argv_free(h); return 1; }

    wchar_t wline[8192];
    MultiByteToWideChar(CP_UTF8, 0, line, -1, wline, 8192);
    int wn = 0;
    wchar_t **wargv = CommandLineToArgvW(wline, &wn);
    if (!wargv) { printf("FAIL %s (CommandLineToArgvW)\n", name); free(line); osx_argv_free(h); return 1; }

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
    osx_argv_free(h);
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
