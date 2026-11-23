/* core:bits -- IEEE-754 bit reinterpretation. memcpy is the defined way to
   read one object's bytes as another type in C; Tycho has no cast for it. */
#include <stdint.h>
#include <string.h>

uint32_t bitsx_f32_bits(float v) {
    uint32_t u;
    memcpy(&u, &v, sizeof u);
    return u;
}

float bitsx_f32_from_bits(uint32_t u) {
    float v;
    memcpy(&v, &u, sizeof v);
    return v;
}

uint64_t bitsx_f64_bits(double v) {
    uint64_t u;
    memcpy(&u, &v, sizeof u);
    return u;
}

double bitsx_f64_from_bits(uint64_t u) {
    double v;
    memcpy(&v, &u, sizeof v);
    return v;
}
