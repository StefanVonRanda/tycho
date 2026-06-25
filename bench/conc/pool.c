/* Worker pool: 1 producer -> bounded ring channel(256) -> K = online-cores
 * worker threads, each running the same MINSTD kernel and accumulating a
 * per-worker partial. A mutex + two condvars guard a fixed-capacity int64 ring
 * (the C analog of the bounded channel) with an explicit `closed` flag; workers
 * drain until empty+closed. Same kernel, cap, and K-picks-cores rule as the
 * other contenders. Checksum: sum of the kernel over 1e6 jobs. */
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#define N 1000000
#define CAP 256
#define ROUNDS 50

static long work(long seed) {
    long x = (seed + 1) % 2147483647;
    for (int k = 0; k < ROUNDS; k++) x = (x * 48271) % 2147483647;
    return x;
}

/* bounded blocking ring of longs */
static long ring[CAP];
static int  head = 0, tail = 0, count = 0, closed = 0;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  not_full = PTHREAD_COND_INITIALIZER;
static pthread_cond_t  not_empty = PTHREAD_COND_INITIALIZER;

static void chan_send(long v) {
    pthread_mutex_lock(&mu);
    while (count == CAP) pthread_cond_wait(&not_full, &mu);
    ring[tail] = v; tail = (tail + 1) % CAP; count++;
    pthread_cond_signal(&not_empty);
    pthread_mutex_unlock(&mu);
}
/* returns 1 + value via *out, or 0 when the channel is closed AND drained */
static int chan_recv(long *out) {
    pthread_mutex_lock(&mu);
    while (count == 0 && !closed) pthread_cond_wait(&not_empty, &mu);
    if (count == 0 && closed) { pthread_mutex_unlock(&mu); return 0; }
    *out = ring[head]; head = (head + 1) % CAP; count--;
    pthread_cond_signal(&not_full);
    pthread_mutex_unlock(&mu);
    return 1;
}
static void chan_close(void) {
    pthread_mutex_lock(&mu);
    closed = 1;
    pthread_cond_broadcast(&not_empty);
    pthread_mutex_unlock(&mu);
}

static void *worker(void *arg) {
    long *partial = (long *)arg;
    long local = 0, j;
    while (chan_recv(&j)) local += work(j);
    *partial = local;
    return NULL;
}

int main(void) {
    long k = sysconf(_SC_NPROCESSORS_ONLN);
    if (k < 1) k = 1;
    pthread_t *ts = malloc((size_t)k * sizeof(pthread_t));
    long *partials = calloc((size_t)k, sizeof(long));
    for (long w = 0; w < k; w++) pthread_create(&ts[w], NULL, worker, &partials[w]);
    for (long i = 0; i < N; i++) chan_send(i);
    chan_close();
    long sum = 0;
    for (long w = 0; w < k; w++) { pthread_join(ts[w], NULL); sum += partials[w]; }
    printf("%ld", sum);
    free(ts); free(partials);
    return 0;
}
