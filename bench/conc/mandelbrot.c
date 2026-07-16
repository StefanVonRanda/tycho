/* Compute-bound parallel reduction over FLOAT work -- C, pthreads, one row-chunk
 * per core. Mirrors mandelbrot.ty op-for-op. The chaotic escape map would diverge
 * across ports if a compiler fused `2.0*zx*zy + cy` into an FMA, so the multiply is
 * materialized into a rounded double `m` before the add -- that makes the kernel
 * fusion-proof under plain -O3 in every port (no per-language float flag needed). */
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

#define W 1200
#define H 1200
#define MAXIT 2000

static int escape(double cx, double cy) {
    double zx = 0.0, zy = 0.0;
    int i = 0;
    for (; i < MAXIT; i++) {
        double x2 = zx * zx;
        double y2 = zy * zy;
        if (x2 + y2 > 4.0) return i;
        double m = 2.0 * zx * zy;
        zy = m + cy;
        zx = x2 - y2 + cx;
    }
    return MAXIT;
}

typedef struct { int lo, hi; long total, inset; } Job;

static const double xmin = 0.0 - 2.5, xmax = 1.0;
static const double ymin = 0.0 - 1.25, ymax = 1.25;

static void *run(void *p) {
    Job *j = (Job *)p;
    long total = 0, inset = 0;
    for (int py = j->lo; py < j->hi; py++) {
        double cy = ymin + (ymax - ymin) * (double)py / (double)H;
        for (int px = 0; px < W; px++) {
            double cx = xmin + (xmax - xmin) * (double)px / (double)W;
            int e = escape(cx, cy);
            total += e;
            if (e >= MAXIT) inset++;
        }
    }
    j->total = total; j->inset = inset;
    return 0;
}

int main(void) {
    long k = sysconf(_SC_NPROCESSORS_ONLN);
    if (k < 1) k = 1;
    if (k > 64) k = 64;
    pthread_t th[64];
    Job jobs[64];
    for (long c = 0; c < k; c++) {
        jobs[c].lo = (int)(H * c / k);
        jobs[c].hi = (int)(H * (c + 1) / k);
        pthread_create(&th[c], 0, run, &jobs[c]);
    }
    long total = 0, inset = 0;
    for (long c = 0; c < k; c++) { pthread_join(th[c], 0); total += jobs[c].total; inset += jobs[c].inset; }
    printf("%ld %ld", total, inset);
    return 0;
}
