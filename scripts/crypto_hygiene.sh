#!/bin/sh
# Does core:crypto leave secret material in memory it has released?
#
# `make corelib` runs corelib/test/crypto and checks the ANSWERS -- that a
# ciphertext decrypts, that a signature verifies. Every one of those passes
# whether or not the plaintext is still sitting in a freed heap block, because
# hygiene has no output. Nothing in this tree looked at it until now.
#
# The instrument is `-Wl,--wrap=free`, which interposes only the frees compiled
# INTO the shim -- libcrypto's own allocator calls are in another object and are
# untouched, so a hit names this file. malloc_usable_size gives the released
# block's real length, so the whole block is scanned rather than a guessed prefix.
#
# TWO CONTROLS, because a scanner that reports zero is indistinguishable from one
# that is not scanning:
#   [c1] a block deliberately left holding the needle must be FOUND.
#   [c2] a block cleansed before free must NOT be found -- otherwise [c1] would
#        pass on a scanner that reported every block.
#
# A NOTE ON THE FIRST VERSION OF THIS PROBE, kept because it cost a wrong verdict:
# it held two shim results at once and printed "roundtrip BROKEN". That was the
# probe. out_hex() recycles ONE buffer per thread, so a C caller must copy each
# return; Tycho callers are unaffected because the compiler copies at the FFI
# boundary (measured: two live core:crypto results are distinct strings).
set -eu

cd "$(dirname "$0")/.."
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

cat > "$T/p.c" <<'EOF'
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
extern void __real_free(void *);
static const char NEEDLE[] = "SECRETSECRETSECRETSECRETSECRET42";
static int hits = 0, frees = 0;
void __wrap_free(void *p) {
    if (p) {
        size_t n = malloc_usable_size(p);
        frees++;
        if (n >= sizeof NEEDLE - 1 && memmem(p, n, NEEDLE, sizeof NEEDLE - 1)) hits++;
    }
    __real_free(p);
}
typedef long long tycho_int;
void *cx_key_random(tycho_int);
const char *cx_random_hex(tycho_int);
const char *cx_key_export_hex(void *);
const char *cx_aead_encrypt(void *, const char *, const char *, const char *);
const char *cx_aead_decrypt(void *, const char *, const char *, const char *);
void OPENSSL_cleanse(void *, size_t);

/* The control's own malloc/free pair is DEAD CODE to gcc, which elided it at -O1
   and made the control report "not found" against a working scanner: with the pair
   gone there is no call left to wrap, so the counter read frees==0. Neither
   escaping the pointer through a global nor an asm memory clobber stopped it --
   the whole probe is built at -O0 instead. Nothing measured here depends on the
   optimization level: OPENSSL_cleanse is an external call either way. */

int main(void) {
    int bad = 0;
    /* [c1] a dirty block must be found, [c2] a cleansed one must not */
    char *d = malloc(64); memcpy(d, NEEDLE, sizeof NEEDLE - 1);
    hits = 0; frees = 0; free(d);
    if (hits != 1) { printf("CONTROL DEAD: hits=%d frees=%d (frees==0 means __wrap_free never ran)\n", hits, frees); return 1; }
    char *c = malloc(64); memcpy(c, NEEDLE, sizeof NEEDLE - 1);
    OPENSSL_cleanse(c, 64); hits = 0; free(c);
    if (hits != 0) { printf("CONTROL DEAD: a CLEANSED block was reported -- the scan matches anything\n"); return 1; }

    char hex[256];
    for (size_t i = 0; i < sizeof NEEDLE - 1; i++)
        sprintf(hex + 2 * i, "%02x", (unsigned char)NEEDLE[i]);
    void *k = cx_key_random(32);
    /* each return copied: out_hex recycles one buffer per thread (see header) */
    char *nonce = strdup(cx_random_hex(12));
    hits = 0; frees = 0;
    char *ct = strdup(cx_aead_encrypt(k, nonce, hex, ""));
    if (hits) { printf("LEAK: encrypt released %d of %d blocks still holding the plaintext\n", hits, frees); bad = 1; }
    hits = 0; frees = 0;
    const char *pt = cx_aead_decrypt(k, nonce, ct, "");
    if (strcmp(pt, hex) != 0) { printf("the round trip does not work -- the probe is measuring nothing\n"); return 1; }
    if (hits) { printf("LEAK: decrypt released %d of %d blocks still holding the plaintext\n", hits, frees); bad = 1; }
    printf("  aead: plaintext found in %s released block\n", bad ? "a" : "no");
    free(nonce); free(ct);
    return bad;
}
EOF

cc -O0 -g -Wl,--wrap=free -o "$T/p" "$T/p.c" corelib/crypto/crypto_shim.c -lcrypto 2>"$T/cc.log" || {
    echo "crypto-hygiene: SKIPPED (cannot link against libcrypto)"; head -3 "$T/cc.log"; exit 0; }

"$T/p" || { echo "crypto-hygiene: FAIL"; exit 1; }
echo "crypto-hygiene: green (a control block holding the secret is found and a cleansed one is not, then neither aead_encrypt nor aead_decrypt releases a heap block still holding the plaintext)"
