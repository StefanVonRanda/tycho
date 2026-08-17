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
