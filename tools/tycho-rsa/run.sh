set -u
cd "$(dirname "$0")/../.." || exit 2          # repo root
TYCHOC="${TYCHOC:-./tychoc1}"
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
