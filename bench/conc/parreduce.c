/* Compute-bound parallel reduction -- C, pthreads, one chunk per core. */
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

#define N 400000000L

typedef struct { long lo, hi, out; } Job;

static void *run(void *p) {
    Job *j = (Job *)p;
    long acc = 0;
    for (long i = j->lo; i < j->hi; i++) acc += (i * 31 + 7) % 1000003;
    j->out = acc;
    return 0;
}

int main(void) {
    long k = sysconf(_SC_NPROCESSORS_ONLN);
    if (k < 1) k = 1;
    if (k > 64) k = 64;
    pthread_t th[64];
    Job jobs[64];
    for (long c = 0; c < k; c++) {
        jobs[c].lo = N * c / k;
        jobs[c].hi = N * (c + 1) / k;
        pthread_create(&th[c], 0, run, &jobs[c]);
    }
    long total = 0;
    for (long c = 0; c < k; c++) { pthread_join(th[c], 0); total += jobs[c].out; }
    printf("%ld", total);
    return 0;
}
