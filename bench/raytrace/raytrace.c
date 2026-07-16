/* Raytrace head-to-head — C reference port. Mirrors raytrace.ty operation-for-
 * operation: same struct-by-value Vec3, same shading math, same checksum fold.
 * Build with -ffp-contract=off so no FMA fusion changes the bit pattern vs the
 * other ports (see run.sh). */
#include <stdio.h>
#include <math.h>

typedef struct { double x, y, z; } Vec3;
typedef struct { Vec3 center; double radius; Vec3 color; } Sphere;

static Vec3 vadd(Vec3 a, Vec3 b)   { return (Vec3){a.x + b.x, a.y + b.y, a.z + b.z}; }
static Vec3 vsub(Vec3 a, Vec3 b)   { return (Vec3){a.x - b.x, a.y - b.y, a.z - b.z}; }
static Vec3 vscale(Vec3 a, double s){ return (Vec3){a.x * s, a.y * s, a.z * s}; }
static double vdot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static double vlen(Vec3 a)         { return sqrt(vdot(a, a)); }
static Vec3 vnorm(Vec3 a)          { double l = vlen(a); return vscale(a, 1.0 / l); }

static double hit_sphere(Vec3 o, Vec3 d, Sphere s) {
    Vec3 oc = vsub(o, s.center);
    double b = vdot(oc, d);
    double c = vdot(oc, oc) - s.radius * s.radius;
    double disc = b * b - c;
    if (disc < 0.0) return 0.0 - 1.0;
    double t = 0.0 - b - sqrt(disc);
    if (t < 0.001) return 0.0 - 1.0;
    return t;
}

static double clamp01(double x) {
    if (x < 0.0) return 0.0;
    if (x > 1.0) return 1.0;
    return x;
}

int main(void) {
    int nx = 1600, ny = 1200, ns = 40;

    Sphere scene[64];
    int nsphere = 0;
    for (int k = 0; k < ns; k++) {
        int col = k % 5;
        int row = k / 5;
        double cx = ((double)col - 2.0) * 0.9;
        double cy = 0.0 - 0.2;
        double cz = 0.0 - 2.5 - (double)row * 0.8;
        double rad = 0.35;
        double cr = (double)((k * 37) % 100) / 100.0;
        double cg = (double)((k * 53) % 100) / 100.0;
        double cb = (double)((k * 97) % 100) / 100.0;
        scene[nsphere++] = (Sphere){{cx, cy, cz}, rad, {cr, cg, cb}};
    }
    scene[nsphere++] = (Sphere){{0.0, 0.0 - 100.5, 0.0 - 3.0}, 100.0, {0.6, 0.6, 0.6}};

    Vec3 light  = vnorm((Vec3){0.0 - 1.0, 1.0, 0.5});
    Vec3 origin = (Vec3){0.0, 0.0, 0.0};

    long long acc = 0;

    for (int j = 0; j < ny; j++) {
        for (int i = 0; i < nx; i++) {
            double u = ((double)i / (double)nx) * 2.0 - 1.0;
            double v = 1.0 - ((double)j / (double)ny) * 2.0;
            Vec3 dir = vnorm((Vec3){u * 1.5, v, 0.0 - 1.0});

            double best = 1000000.0;
            int hit = 0 - 1;
            for (int m = 0; m < nsphere; m++) {
                double t = hit_sphere(origin, dir, scene[m]);
                if (t > 0.0) {
                    if (t < best) { best = t; hit = m; }
                }
            }

            double r = 0.0, g = 0.0, b = 0.0;
            if (hit < 0) {
                double tt = 0.5 * (dir.y + 1.0);
                r = (1.0 - tt) + tt * 0.5;
                g = (1.0 - tt) + tt * 0.7;
                b = (1.0 - tt) + tt * 1.0;
            } else {
                Sphere s = scene[hit];
                Vec3 p = vadd(origin, vscale(dir, best));
                Vec3 n = vnorm(vsub(p, s.center));
                double diff = vdot(n, light);
                if (diff < 0.0) diff = 0.0;
                double sh = 0.15 + 0.85 * diff;
                r = s.color.x * sh;
                g = s.color.y * sh;
                b = s.color.z * sh;
            }

            long long ir = (long long)(clamp01(r) * 255.0);
            long long ig = (long long)(clamp01(g) * 255.0);
            long long ib = (long long)(clamp01(b) * 255.0);
            acc = acc + ir + ig * 7 + ib * 13;
        }
    }

    printf("%lld\n", acc);
    return 0;
}
