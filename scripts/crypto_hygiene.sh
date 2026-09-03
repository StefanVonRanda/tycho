set -eu

cd "$(dirname "$0")/.."
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
# Each control is a PATCHED COPY of the shim in "$T/<name>/", and the shim opens
# with #include "../tycho.h" -- resolved against the copy's own directory, so it
# looks here. Without this line both controls fail to BUILD: [2] took the skip
# branch and printed a green verdict claiming it had run, and [3] read the build
# failure as "the control disagreed" and passed on a control that never ran.
cp corelib/tycho.h "$T/tycho.h"

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

# --- [3] the branch-free decode must still be the SAME decode ----------------
# A hex parser rewritten in bit tricks is exactly the code that stays plausible
# while being wrong: &0xFF is what stops a non-digit's wrapped subtraction from
# testing as valid, and dropping it accepts junk silently. So the new classifier
# is compared against the branching original over ALL 256 byte values, plus
# accept/reject cases either side of every boundary ('/' ':' '@' 'G' '`' 'g')
# and one byte-exact decode. The control removes a mask and must be caught.
cat > "$T/e.c" <<'EOF'
#include <stdio.h>
#include <string.h>
#include "crypto_shim.c"
static int hexval_old(int c) {                 /* the pre-2026-08-15 version */
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
int main(void) {
    int diff = 0;
    for (unsigned c = 0; c < 256; c++) {
        unsigned v = 0, ok = hexval_ct(c, &v);
        int o = hexval_old((int)c);
        if ((o >= 0) != (ok != 0) || (o >= 0 && (unsigned)o != v)) {
            if (!diff) printf("  DIFF at 0x%02x: old=%d new ok=%u v=%u\n", c, o, ok, v);
            diff++;
        }
    }
    struct { const char *in; int want; } t[] = {
        {"", 0}, {"00", 0}, {"ff", 0}, {"FF", 0}, {"deadBEEF", 0},
        {"f", 1}, {"abc", 1}, {"0g", 1}, {"g0", 1}, {"00gg", 1},
        {"zz", 1}, {"0/", 1}, {"0:", 1}, {"`0", 1}, {"0@", 1}, {"0G", 1},
    };
    int bad = 0; size_t n; unsigned char *b;
    for (size_t i = 0; i < sizeof t / sizeof *t; i++) {
        n = 0; b = hexdec(t[i].in, &n);
        if ((b == NULL) != t[i].want) { printf("  WRONG: %-8s rejected=%d want=%d\n", t[i].in, b == NULL, t[i].want); bad++; }
        free(b);
    }
    n = 0; b = hexdec("0f1e2d3c", &n);
    if (!b || n != 4 || b[0] != 0x0f || b[1] != 0x1e || b[2] != 0x2d || b[3] != 0x3c) { printf("  WRONG bytes\n"); bad++; }
    free(b);
    if (diff || bad) { printf("  %d byte values differ, %d cases wrong\n", diff, bad); return 1; }
    printf("  hexdec: all 256 byte values and %zu accept/reject cases match the branching original\n", sizeof t / sizeof *t);
    return 0;
}
EOF
mkdir -p "$T/mask"
sed 's|unsigned x = ((c \| 0x20u) - .a.) & 0xFFu;|unsigned x = ((c \| 0x20u) - 0x61u);|' \
    corelib/crypto/crypto_shim.c > "$T/mask/crypto_shim.c"
grep -q "0x61u);" "$T/mask/crypto_shim.c" || {
    echo "crypto-hygiene: FAIL -- the [3] control patch did not apply"; exit 1; }
cc -O1 -I "$T/mask" -o "$T/em" "$T/e.c" -lcrypto 2>"$T/mask.log" || {
    echo "crypto-hygiene: FAILED -- the [3] control does not BUILD, so it cannot disagree"
    head -3 "$T/mask.log"; exit 1; }
if "$T/em" >/dev/null 2>&1; then
    echo "CONTROL DEAD: a decode with the &0xFF mask removed compared EQUAL to the"
    echo "              original, so [3] would pass on a broken classifier."
    exit 1
fi
cc -O1 -I corelib/crypto -o "$T/e" "$T/e.c" -lcrypto 2>/dev/null || {
    echo "crypto-hygiene: FAILED (the equivalence probe does not build)"; exit 1; }
"$T/e" || { echo "crypto-hygiene: FAIL"; exit 1; }

ct_claim=""
if command -v valgrind >/dev/null 2>&1 && [ -f /usr/include/valgrind/memcheck.h ]; then
    cat > "$T/ct.c" <<'EOF'
#include <valgrind/memcheck.h>
#include <stdio.h>
#include <string.h>
#include "crypto_shim.c"
int main(void) {
    char hex[65];
    memcpy(hex, "0f1e2d3c4b5a69788796a5b4c3d2e1f00f1e2d3c4b5a69788796a5b4c3d2e1f0", 65);
    VALGRIND_MAKE_MEM_UNDEFINED(hex, 64);          /* the secret key material */
    size_t n = 0; unsigned bad = 0;
    unsigned char *b = hexdec_ct(hex, &n, &bad);
    VALGRIND_MAKE_MEM_DEFINED(&bad, sizeof bad);   /* the one public bit */
    VALGRIND_MAKE_MEM_DEFINED(&n, sizeof n);
    printf("%s n=%zu\n", bad ? "rejected" : "decoded", n);
    if (b) free(b);
    return 0;
}
EOF
    printf '{\n  length-is-public-cond\n  Memcheck:Cond\n  fun:strlen\n  fun:hexdec_ct\n}\n{\n  length-is-public-value8\n  Memcheck:Value8\n  fun:strlen\n  fun:hexdec_ct\n}\n' > "$T/sup"

    mkdir -p "$T/ctl"
    cp corelib/crypto/crypto_shim.c "$T/ctl/crypto_shim.c"
    sed -i 's|\*v = is_d \* d + is_x \* (x + 10);|if (is_d) *v = d; else if (is_x) *v = x + 10; else *v = 0;|' "$T/ctl/crypto_shim.c"
    grep -q 'if (is_d) \*v' "$T/ctl/crypto_shim.c" || {
        echo "crypto-hygiene: FAIL -- the control patch did not apply, so [2] would prove nothing"; exit 1; }

    ct_run() {   # $1 = include dir; echoes the number of reported leaks
        cc -O1 -g -I "$1" -o "$T/ct" "$T/ct.c" -lcrypto 2>"$T/ct.cc.log" || { echo skip; return; }
        valgrind -q --suppressions="$T/sup" "$T/ct" >/dev/null 2>"$T/ct.err"
        grep -c uninitialised "$T/ct.err" || true
    }
    ctl=$(ct_run "$T/ctl")
    if [ "$ctl" = skip ]; then
        echo "crypto-hygiene: SKIPPED [2] (cannot build the ctgrind probe)"
        head -3 "$T/ct.cc.log"
    elif [ "$ctl" -lt 1 ]; then
        echo "CONTROL DEAD: a branching digit decode was reported clean -- the"
        echo "              suppressions are hiding the subject, so [2] proves nothing."
        exit 1
    else
        real=$(ct_run corelib/crypto)
        if [ "$real" != 0 ]; then
            echo "LEAK: the key-import hex decode branches on the secret ($real reports)"
            grep -A2 uninitialised "$T/ct.err" | head -6
            echo "crypto-hygiene: FAIL"; exit 1
        fi
        echo "  hexdec: 0 secret-dependent branches (the branching control scored $ctl)"
        ct_claim="; and under memcheck the key-import hex decode has no branch derived from the secret, with only the input length and the one well-formedness bit declassified, against a branching control that does redden"
    fi
else
    echo "crypto-hygiene: SKIPPED [2] (no valgrind or no memcheck.h)"
fi
# The verdict names only the legs that RAN: $ct_claim is set by [2]'s own else
# branch and stays empty on either skip, so a skipped [2] cannot be claimed.
echo "crypto-hygiene: green (a control block holding the secret is found and a cleansed one is not, then neither aead_encrypt nor aead_decrypt releases a heap block still holding the plaintext; the branch-free hex decode classifies all 256 byte values exactly as the branching original did, against a control with a mask removed that does not${ct_claim})"
