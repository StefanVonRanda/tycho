/* core:hash -- CRC-32 (IEEE 802.3 / zlib, reflected, polynomial 0xEDB88320).
 * Slicing-by-8 over tables built once before main, or the ARMv8 CRC32
 * instructions when the compiler targets them (same polynomial, same result).
 * The bit-at-a-time Tycho loop it replaces ran at 160 MB/s; a pak checked on
 * every read cannot live with that (FRICTION 237). Tycho has no global tables,
 * so the table is the shim's. */
#include <stdint.h>
#include <string.h>
#if defined(__ARM_FEATURE_CRC32)
#include <arm_acle.h>
#endif

static uint32_t hashx_tab[8][256];

__attribute__((constructor)) static void hashx_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : c >> 1;
        hashx_tab[0][i] = c;
    }
    for (uint32_t i = 0; i < 256; i++)
        for (int t = 1; t < 8; t++)
            hashx_tab[t][i] = (hashx_tab[t - 1][i] >> 8) ^ hashx_tab[0][hashx_tab[t - 1][i] & 0xFF];
}

/* crc: a previous result (0 to start), as zlib's crc32(crc, buf, len). */
int64_t hashx_crc32(int64_t crc, const char *data, int64_t n) {
    const unsigned char *p = (const unsigned char *)data;
    uint32_t c = ~(uint32_t)crc;
    if (n <= 0) return (int64_t)(uint32_t)~c;
#if defined(__ARM_FEATURE_CRC32)
    while (n >= 8) {
        uint64_t w;
        memcpy(&w, p, 8);
        c = __crc32d(c, w);
        p += 8;
        n -= 8;
    }
    while (n-- > 0) c = __crc32b(c, *p++);
#else
    while (n >= 8) {
        uint32_t lo, hi;
        memcpy(&lo, p, 4);
        memcpy(&hi, p + 4, 4);
        lo ^= c;   /* little-endian hosts; the reflected CRC reads bytes in memory order */
        c = hashx_tab[7][lo & 0xFF] ^ hashx_tab[6][(lo >> 8) & 0xFF] ^
            hashx_tab[5][(lo >> 16) & 0xFF] ^ hashx_tab[4][lo >> 24] ^
            hashx_tab[3][hi & 0xFF] ^ hashx_tab[2][(hi >> 8) & 0xFF] ^
            hashx_tab[1][(hi >> 16) & 0xFF] ^ hashx_tab[0][hi >> 24];
        p += 8;
        n -= 8;
    }
    while (n-- > 0) c = hashx_tab[0][(c ^ *p++) & 0xFF] ^ (c >> 8);
#endif
    return (int64_t)(uint32_t)~c;
}

/* The bytes form: a Tycho `bytes` argument crosses as (pointer, length). */
int64_t hashx_crc32_b(int64_t crc, const char *data, int64_t n) {
    return hashx_crc32(crc, data, n);
}
