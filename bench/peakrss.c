/* Run a command, report its peak resident set size and wall-clock time.
 *
 *   peakrss <program> [args...]
 *
 * Prints "<rss> <ms>" to STDERR (so the child's own stdout passes through
 * untouched) and exits with the child's exit status. `rss` is ru_maxrss,
 * which is KILOBYTES on Linux and BYTES on macOS/BSD — bench/run.sh normalizes
 * by `uname`. This is what lets `make bench` assert the thesis's memory claims:
 * the in-place optimizations keep peak RSS flat (MBs) where the naive
 * copy-each-step baseline is quadratic (hundreds of MB to GBs). */
#include <stdio.h>
#include <time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: peakrss <program> [args...]\n"); return 2; }
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    pid_t pid = fork();
    if (pid == 0) { execvp(argv[1], argv + 1); _exit(127); }
    int st;
    struct rusage ru;
    wait4(pid, &st, 0, &ru);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    long ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
    fprintf(stderr, "%ld %ld\n", ru.ru_maxrss, ms);
    return (WIFEXITED(st) && WEXITSTATUS(st) == 0) ? 0 : 1;
}
