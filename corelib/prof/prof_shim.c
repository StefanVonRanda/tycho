/* core:prof float bit transfer for the Spall stream header. */
#include <stdint.h>
#include <string.h>

uint64_t prx_f64_bits(double v) {
    uint64_t u;
    memcpy(&u, &v, sizeof u);
    return u;
}

double prx_f64_from_bits(uint64_t u) {
    double v;
    memcpy(&v, &u, sizeof v);
    return v;
}
