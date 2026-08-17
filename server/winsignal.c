#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void quote_into(char *o, const char *s, size_t *used) {
    char *start = o;
    int simple = *s != '\0';
    for (const char *p = s; *p; p++)
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\v' || *p == '"') { simple = 0; break; }
    if (simple) {
        size_t n = strlen(s);
        memcpy(o, s, n); o += n;
    } else {
        *o++ = '"';
        for (const char *p = s; ; p++) {
            size_t nsl = 0;
            while (*p == '\\') { nsl++; p++; }
            if (*p == '\0') { for (size_t i = 0; i < nsl * 2; i++) *o++ = '\\'; break; }
            if (*p == '"')   { for (size_t i = 0; i < nsl * 2 + 1; i++) *o++ = '\\'; *o++ = '"'; }
            else             { for (size_t i = 0; i < nsl; i++) *o++ = '\\'; *o++ = *p; }
        }
        *o++ = '"';
    }
    *used = (size_t)(o - start);
}

static int do_spawn(int argc, char **argv) {
    const char *pidfile = argv[2];
    size_t cap = 32768;
    char *line = calloc(1, cap);
    if (!line) return 2;
    size_t len = 0;
    for (int i = 3; i < argc; i++) {
        if (i > 3) line[len++] = ' ';
        size_t used = 0;
        quote_into(line + len, argv[i], &used);
        len += used;
        if (len + 4096 > cap) { fprintf(stderr, "winsignal: command line too long\n"); free(line); return 2; }
    }
    line[len] = '\0';

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    memset(&pi, 0, sizeof pi);
    si.cb = sizeof si;
    /* CREATE_NEW_PROCESS_GROUP is the whole point: it makes the child a group
     * leader so `break` can target it alone. The child stays on THIS console,
     * so the harness's redirections of stdout/stderr are inherited unchanged.
     * Note the child starts with Ctrl-C disabled (a documented consequence of
     * the flag) -- CTRL_BREAK is unaffected, which is why `break` sends that. */
    if (!CreateProcessA(NULL, line, NULL, NULL, TRUE, CREATE_NEW_PROCESS_GROUP,
                        NULL, NULL, &si, &pi)) {
        fprintf(stderr, "winsignal: CreateProcess failed (%lu): %s\n",
                (unsigned long)GetLastError(), line);
        free(line);
        return 2;
    }
    free(line);

    FILE *f = fopen(pidfile, "w");
    if (!f) {
        fprintf(stderr, "winsignal: cannot write pidfile %s\n", pidfile);
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        return 2;
    }
    fprintf(f, "%lu\n", (unsigned long)pi.dwProcessId);
    fclose(f);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: winsignal spawn <pidfile> <prog> [args...]\n"
                        "       winsignal break|alive|kill <winpid>\n");
        return 2;
    }
    if (!strcmp(argv[1], "spawn")) {
        if (argc < 4) { fprintf(stderr, "winsignal: spawn needs a pidfile and a program\n"); return 2; }
        return do_spawn(argc, argv);
    }

    DWORD pid = (DWORD)strtoul(argv[2], NULL, 10);
    if (pid == 0) { fprintf(stderr, "winsignal: bad pid %s\n", argv[2]); return 2; }

    if (!strcmp(argv[1], "break")) {
        if (!GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid)) {
            fprintf(stderr, "winsignal: GenerateConsoleCtrlEvent(%lu) failed (%lu)\n",
                    (unsigned long)pid, (unsigned long)GetLastError());
            return 1;
        }
        return 0;
    }

    /* SYNCHRONIZE is the least right that answers "has it exited"; a process
     * that has exited but whose handle is still open answers STILL_ACTIVE only
     * if we ask for the exit code, so ask for that. */
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, pid);
    if (!h) return 1;                       /* gone, or not ours -- both are "not alive" */
    int rc = 1;
    if (!strcmp(argv[1], "alive")) {
        DWORD code = 0;
        rc = (GetExitCodeProcess(h, &code) && code == STILL_ACTIVE) ? 0 : 1;
    } else if (!strcmp(argv[1], "kill")) {
        rc = TerminateProcess(h, 137) ? 0 : 1;
    } else {
        fprintf(stderr, "winsignal: unknown command %s\n", argv[1]);
        rc = 2;
    }
    CloseHandle(h);
    return rc;
}
