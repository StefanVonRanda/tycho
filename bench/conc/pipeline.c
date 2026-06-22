/* Channel pipeline -- C, hand-rolled bounded ring (mutex + condvars),
 * 1 producer -> 4 consumers. Payloads are malloc'd strings passed BY
 * POINTER (no copy) and freed by the consumer -- C's manual-ownership
 * fast path, the unsafety tycho's deep copies buy out. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define CAP 256
#define N 1000000
#define W 4

static char *ring[CAP];
static long head, count;
static int closed;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t not_full = PTHREAD_COND_INITIALIZER, not_empty = PTHREAD_COND_INITIALIZER;

static void send(char *s) {
    pthread_mutex_lock(&mu);
    while (count == CAP) pthread_cond_wait(&not_full, &mu);
    ring[(head + count) % CAP] = s;
    count++;
    pthread_cond_signal(&not_empty);
    pthread_mutex_unlock(&mu);
}

static char *recv(void) {
    pthread_mutex_lock(&mu);
    while (count == 0 && !closed) pthread_cond_wait(&not_empty, &mu);
    if (count == 0) { pthread_mutex_unlock(&mu); return 0; }
    char *s = ring[head];
    head = (head + 1) % CAP;
    count--;
    pthread_cond_signal(&not_full);
    pthread_mutex_unlock(&mu);
    return s;
}

static void *consumer(void *p) {
    long c = 0;
    char *s;
    while ((s = recv())) { c += (long)strlen(s); free(s); }
    *(long *)p = c;
    return 0;
}

int main(void) {
    pthread_t th[W];
    long counts[W];
    for (int w = 0; w < W; w++) pthread_create(&th[w], 0, consumer, &counts[w]);
    char buf[32];
    for (long i = 0; i < N; i++) {
        int len = snprintf(buf, sizeof buf, "item-%ld", i);
        char *s = (char *)malloc((size_t)len + 1);
        memcpy(s, buf, (size_t)len + 1);
        send(s);
    }
    pthread_mutex_lock(&mu);
    closed = 1;
    pthread_cond_broadcast(&not_empty);
    pthread_mutex_unlock(&mu);
    long total = 0;
    for (int w = 0; w < W; w++) { pthread_join(th[w], 0); total += counts[w]; }
    printf("%ld", total);
    return 0;
}
