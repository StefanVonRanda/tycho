/* core:cbor float bit transfer -- CBOR stores IEEE-754 bits, and Tycho's
 * numeric conversions do not reinterpret. memcpy is the defined bit copy. */
#include <stdint.h>
#include <string.h>

uint64_t cbx_f64_bits(double v) {
    uint64_t u;
    memcpy(&u, &v, sizeof u);
    return u;
}

double cbx_f64_from_bits(uint64_t u) {
    double v;
    memcpy(&v, &u, sizeof v);
    return v;
}

uint32_t cbx_f32_bits(double v) {
    float f = (float)v;
    uint32_t u;
    memcpy(&u, &f, sizeof u);
    return u;
}

double cbx_f32_from_bits(uint32_t u) {
    float f;
    memcpy(&f, &u, sizeof f);
    return (double)f;
}
