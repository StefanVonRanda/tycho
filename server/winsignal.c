/* winsignal -- deliver a console control event to ONE process on Windows.
 * WINDOWS ONLY; server/run.sh builds it there and nowhere else.
 *
 * WHY IT EXISTS. server/run.sh's six shutdown cases drive the server with
 * `kill -TERM`. MSYS2's kill cannot signal a NATIVE PE -- it terminates it --
 * so the graceful path never ran, the "stopped after N requests" line never
 * printed, and the lane BLOCKED waiting for a wind-down that could not happen
 * (measured 2026-08-08: 43 minutes before it was killed by hand, which is worse
 * than a red because a hang stops the whole sweep).
 *
 * WHY NOT GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, 0), which is what the
 * corelib signal test uses. Group 0 means "every process on my console", and
 * this lane's console is the harness shell's -- the sweep would Ctrl-Break
 * itself. Targeting one process needs that process to be a process-GROUP
 * LEADER, and CREATE_NEW_PROCESS_GROUP is a CreationFlag, so bash cannot set it
 * after the fact. Hence `spawn`: this tool launches the server itself with the
 * flag, and `break` then reaches exactly that group.
 *
 * The pid written by `spawn` is a WINDOWS pid. MSYS2's `$!` is an MSYS pid and
 * the console API will not take it; the two are different numbers for the same
 * process. That mismatch is the second reason a naive port of this lane fails.
 *
 *   winsignal spawn <pidfile> <prog> [args...]   run prog in a new process
 *                                                group, write its Windows pid
 *                                                to pidfile, wait, exit its code
 *   winsignal break <winpid>                     CTRL_BREAK to that group
 *   winsignal alive <winpid>                     exit 0 if still running
 *   winsignal kill  <winpid>                     TerminateProcess (the kill -9)
 *
 * Build: gcc -std=c11 -O2 -o winsignal.exe server/winsignal.c
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Quote one argv element per the CommandLineToArgvW rules. Same algorithm as
 * corelib/os/os_shim.c's osx_win_quote and for the same reason -- a server
 * argument carrying a space must not become two arguments. Kept separate
 * rather than shared because this is a standalone harness tool with no corelib
 * link; the shim's copy is the one under test in os_argv_quotecheck.c. */
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
        /* Exit code 137 and not 9: on POSIX the shell reports an uncatchable
         * SIGKILL as 128+9, and server/run.sh's SIGKILL case asserts exactly
         * that number as "nothing wound down". Windows has no signals, so the
         * code is simply whatever the killer chooses -- 9 would carry no
         * meaning here either. Choosing 137 makes the same EVENT report the
         * same way on both platforms instead of forcing the assertion to know
         * which OS it is on. */
        rc = TerminateProcess(h, 137) ? 0 : 1;
    } else {
        fprintf(stderr, "winsignal: unknown command %s\n", argv[1]);
        rc = 2;
    }
    CloseHandle(h);
    return rc;
}
