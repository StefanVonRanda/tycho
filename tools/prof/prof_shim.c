/* Dependency-free statistical CPU-time profiler for hier-compiled binaries.
 *
 * perf is often blocked (kernel.perf_event_paranoid >= 2 with no root), valgrind
 * may be absent, and gprof (-pg) mis-attributes time on tiny high-call-count
 * functions because its mcount instrumentation adds per-call overhead. This shim
 * needs none of that: ITIMER_PROF fires SIGPROF on consumed CPU time, and the
 * handler records the interrupted instruction pointer (RIP) plus a few stack
 * words (return-address candidates). At exit it resolves each sample to a symbol
 * with dladdr and appends "leaf <- caller" lines to $HIER_PROF_OUT (default
 * /tmp/prof_syms.txt — profile.sh points it at a private mktemp dir); many runs
 * accumulate, then `sort | uniq -c | sort -rn` gives the breakdown. Link with the
 * program under test: cc -O2 -no-pie -rdynamic -fno-omit-frame-pointer prog.c
 * prof_shim.c -ldl. (-rdynamic exposes the program's statics to dladdr; -no-pie
 * keeps its addresses fixed across runs.) See README.md.
 */
#define _GNU_SOURCE
#include <signal.h>
#include <sys/time.h>
#include <ucontext.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define CAP (1u << 23)
#define NW 8     /* RIP + 3 RSP words (leaf-call return addr) + 3 frame-pointer-chain returns */
static unsigned long pcs[CAP][NW];
static volatile unsigned long npc = 0;

static void on_prof(int sig, siginfo_t *si, void *uc) {
    (void)sig; (void)si;
    if (npc >= CAP) return;
    ucontext_t *c = (ucontext_t *)uc;
    unsigned long *sp = (unsigned long *)c->uc_mcontext.gregs[REG_RSP];
    unsigned long bp  = (unsigned long)c->uc_mcontext.gregs[REG_RBP];
    unsigned long i = npc++;
    pcs[i][0] = (unsigned long)c->uc_mcontext.gregs[REG_RIP];
    /* a true-leaf libc call (memcpy/strlen) leaves its caller's return addr near
     * RSP; a libc call WITH a frame (malloc) needs the saved-RBP chain to reach
     * the hier function. Capture both; resolution takes the first that's ours. */
    for (int k = 0; k < 3; k++) pcs[i][1 + k] = (sp ? sp[k] : 0);
    for (int k = 0; k < 3; k++) {
        if (!bp || (bp & 7) || bp < (unsigned long)sp) { pcs[i][4 + k] = 0; continue; }
        pcs[i][4 + k] = ((unsigned long *)bp)[1];        /* return address at [rbp+8] */
        unsigned long nb = ((unsigned long *)bp)[0];      /* saved rbp */
        if (nb <= bp) bp = 0; else bp = nb;
    }
}

__attribute__((constructor)) static void prof_start(void) {
    struct sigaction sa;
    sa.sa_sigaction = on_prof;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGPROF, &sa, 0);
    struct itimerval t;
    t.it_interval.tv_sec = 0; t.it_interval.tv_usec = 1000;   /* 1 kHz on CPU time */
    t.it_value = t.it_interval;
    setitimer(ITIMER_PROF, &t, 0);
}

__attribute__((destructor)) static void prof_stop(void) {
    struct itimerval z = {{0,0},{0,0}};
    setitimer(ITIMER_PROF, &z, 0);
    const char *out = getenv("HIER_PROF_OUT");
    if (!out || !*out) out = "/tmp/prof_syms.txt";
    int fd = open(out, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return;
    Dl_info self; dladdr((void *)prof_stop, &self);          /* our own object name */
    for (unsigned long i = 0; i < npc; i++) {
        Dl_info di;
        const char *leaf;
        if (dladdr((void *)pcs[i][0], &di) && di.dli_sname) leaf = di.dli_sname;
        else if (dladdr((void *)pcs[i][0], &di) && di.dli_fname == self.dli_fname) leaf = "[ours-static]";
        else leaf = "[libc]";
        /* caller: first stack word resolving into OUR object (a hier-emitted fn) */
        const char *caller = "?";
        for (int k = 1; k < NW; k++)
            if (dladdr((void *)pcs[i][k], &di) && di.dli_fname == self.dli_fname && di.dli_sname) { caller = di.dli_sname; break; }
        dprintf(fd, "%s <- %s\n", leaf, caller);
    }
    close(fd);
}
