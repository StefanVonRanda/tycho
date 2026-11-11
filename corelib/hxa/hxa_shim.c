/* HxA float bit transfer: the file stores IEEE-754 bits, and Tycho's numeric
 * conversions do not reinterpret. memcpy is the defined bit copy. */
#include <stdint.h>
#include <string.h>

uint32_t hxax_f32_bits(double v) {
    float f = (float)v;
    uint32_t u;
    memcpy(&u, &f, sizeof u);
    return u;
}

double hxax_f32_from_bits(uint32_t u) {
    float f;
    memcpy(&f, &u, sizeof f);
    return (double)f;
}

uint64_t hxax_f64_bits(double v) {
    uint64_t u;
    memcpy(&u, &v, sizeof u);
    return u;
}

double hxax_f64_from_bits(uint64_t u) {
    double v;
    memcpy(&v, &u, sizeof v);
    return v;
}
