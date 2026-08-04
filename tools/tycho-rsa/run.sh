#!/bin/sh
# Gate for tycho-rsa, the pure-Tycho RSA implementation on core:bignum in
# tools/tycho-rsa/. Same shape as the other tool lanes: step [9] tools-check
# --emit-c's every .ty in the tree, so a syntax error already reddens there,
# and [3b] entrypoints never looks under tools/ -- so nothing RAN the tool
# before this lane existed.
#
# WHAT IT ASSERTS:
#   [1] the GROUND-TRUTH DIFFERENTIAL inside `selfcheck`: the textbook RSA
#       vector (p=61 q=53 n=3233 e=17 d=2753: encrypt 65 -> 2790, decrypt
#       back to 65), modexp cross-checked against python 3's pow() at
#       256/512/2048-bit sizes, Miller-Rabin probes (97 prime, 91 composite,
#       561 Carmichael -- passes Fermat, must fail MR), and a
#       deterministic-seeded keygen whose invariants are self-checked:
#       n == p*q, e*d == 1 mod phi, p/q pass is_prime, and both the
#       encrypt->decrypt and sign->verify round-trips return the message.
#   [2] the transcript is golden-locked (expected.out), so the deterministic
#       key (fixed rand.seed) and every "ok" line are recorded assertions.
#   [3] a deeper arithmetic workout: a 512-bit keygen whose encrypt->
#       decrypt round-trip is asserted via the CLI (modexp on the printed
#       n/e/d) -- the mul/divmod path at 16 limbs instead of 8.
#
# Re-record the golden with:  RECORD=1 sh tools/tycho-rsa/run.sh
set -u
cd "$(dirname "$0")/../.." || exit 2          # repo root
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "tycho-rsa: no ./tychoc -- run 'make' first"; exit 2; }
export TYCHO_CORELIB="$PWD/corelib"
RECORD="${RECORD:-0}"
golden="$PWD/tools/tycho-rsa/expected.out"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
bad() { echo "FAIL: $*"; fail=1; }

RSA="$T/tycho-rsa"
if ! "$TYCHOC" tools/tycho-rsa/main.ty -o "$RSA" >"$T/build.log" 2>&1; then
    echo "FAIL: tychoc compile"; sed 's/^/      /' "$T/build.log"
    echo "tycho-rsa: FAIL"; exit 1
fi

# ---------------------------------------------------------------------------
# [1] + [2] the selfcheck transcript vs the golden
# ---------------------------------------------------------------------------
out="$T/all.out"
"$RSA" selfcheck > "$out" 2>"$T/e" || {
    bad "selfcheck exited non-zero"; sed 's/^/      /' "$T/e"
}
if grep -q 'FAIL' "$out"; then
    bad "selfcheck printed FAIL"; grep 'FAIL' "$out" | sed 's/^/      /'
fi

if [ "$RECORD" = 1 ]; then
    if [ "$fail" -ne 0 ]; then echo "FAIL: refusing to RECORD from a failed run"; exit 1; fi
    cp "$out" "$golden"; echo "rec  tycho-rsa"
fi
if [ ! -f "$golden" ]; then
    bad "no golden -- run RECORD=1 sh tools/tycho-rsa/run.sh"
elif ! cmp -s "$out" "$golden"; then
    bad "transcript != golden"; diff "$golden" "$out" | sed 's/^/      /'
fi

# ---------------------------------------------------------------------------
# [3] the 512-bit keygen round-trip through the CLI
# ---------------------------------------------------------------------------
"$RSA" keygen 512 > "$T/k512" 2>/dev/null || { bad "keygen 512 exited non-zero"; fail=1; }
n=$(sed -n 's/^n = //p' "$T/k512")
e=$(sed -n 's/^e = //p' "$T/k512")
d=$(sed -n 's/^d = //p' "$T/k512")
if [ -z "$n" ] || [ -z "$e" ] || [ -z "$d" ]; then
    bad "keygen 512 did not print n/e/d"; sed 's/^/      /' "$T/k512"
else
    m="12345678901234567890123456789012345678901234567890"
    c=$("$RSA" modexp "$m" "$e" "$n" 2>/dev/null)
    back=$("$RSA" modexp "$c" "$d" "$n" 2>/dev/null)
    if [ "$back" != "$m" ]; then
        bad "512-bit roundtrip: decrypt(encrypt(m)) != m"
    fi
fi

if [ "$fail" -eq 0 ]; then
    echo "tycho-rsa: green (textbook vector, 3 python-pow modexp sizes, Miller-Rabin probes incl. Carmichael 561, deterministic keygen invariants + both round-trips == golden; 512-bit keygen round-trip ok)"
else
    echo "tycho-rsa: FAIL"; exit 1
fi
