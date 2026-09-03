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
# [3] the OAEP round trip through the CLI, on the key pinned in main.ty and in
#     kat.py. Keygen at 1024 bits costs ~20 s and OAEP-SHA256 needs a modulus of
#     at least 66 bytes, so a pinned key is what makes this leg affordable at
#     all -- the fresh-key leg is [4], which needs no padding.
# ---------------------------------------------------------------------------
N=$(sed -n '/^fn test_n() -> string:/{n;s/.*return "\([0-9]*\)".*/\1/p;}' tools/tycho-rsa/main.ty)
D=$(sed -n '/^fn test_d() -> string:/{n;s/.*return "\([0-9]*\)".*/\1/p;}' tools/tycho-rsa/main.ty)
[ -n "$N" ] && [ -n "$D" ] || bad "could not read the pinned test key out of main.ty"
PT="6f6e652074776f2074687265650a"
c1=$("$RSA" encrypt "$PT" 65537 "$N" 2>"$T/e") || bad "encrypt exited non-zero: $(cat "$T/e")"
back=$("$RSA" decrypt "$c1" "$D" "$N" 2>"$T/e") || bad "decrypt exited non-zero: $(cat "$T/e")"
[ "$back" = "$PT" ] || bad "oaep CLI round trip: decrypt(encrypt(m)) != m (got $back)"
[ ${#c1} -eq 256 ] || bad "ciphertext is ${#c1} hex chars, want 256 (a full 128-byte block)"

# ---------------------------------------------------------------------------
# [4] the key is drawn fresh: two runs of the same size must not agree.
#     A golden cannot see this -- the transcript no longer prints the digits.
# ---------------------------------------------------------------------------
"$RSA" keygen 512 > "$T/k512" 2>/dev/null || bad "keygen 512 exited non-zero"
"$RSA" keygen 512 > "$T/k512b" 2>/dev/null || bad "second keygen 512 exited non-zero"
n=$(sed -n 's/^n = //p' "$T/k512")
n2=$(sed -n 's/^n = //p' "$T/k512b")
if [ -z "$n" ] || [ -z "$n2" ]; then
    bad "keygen 512 did not print n"
elif [ "$n2" = "$n" ]; then
    bad "two keygen 512 runs produced the SAME modulus -- the generator is not seeded from the OS"
fi

# ---------------------------------------------------------------------------
# [5] the known-answer test against an EXTERNAL oracle, python's `cryptography`,
#     in BOTH directions. One direction alone proves nothing: an encoder and a
#     decoder sharing a mistake round-trip each other perfectly, which is exactly
#     what the tool's own selfcheck cannot rule out.
# ---------------------------------------------------------------------------
if ! python3 tools/tycho-rsa/kat.py "$RSA" > "$T/kat.out" 2>&1; then
    bad "OAEP known-answer test vs python cryptography"; sed 's/^/      /' "$T/kat.out"
fi
grep -q SKIP "$T/kat.out" && echo "      (kat: python cryptography absent -- KAT skipped)"

# ---------------------------------------------------------------------------
# [6] padding is what makes the same plaintext encrypt to a different ciphertext.
#     Textbook RSA cannot do this and no golden can see it: the transcript holds
#     verdicts, not ciphertexts.
# ---------------------------------------------------------------------------
a=$("$RSA" encrypt "$PT" 65537 "$N" 2>/dev/null)
b=$("$RSA" encrypt "$PT" 65537 "$N" 2>/dev/null)
if [ -z "$a" ] || [ -z "$b" ]; then
    bad "encrypt printed nothing"
elif [ "$a" = "$b" ]; then
    bad "the same plaintext encrypted to the SAME ciphertext twice -- the OAEP seed is not random"
fi
[ "$("$RSA" decrypt "$a" "$D" "$N" 2>/dev/null)" = "$PT" ] || bad "first ciphertext did not decrypt"
[ "$("$RSA" decrypt "$b" "$D" "$N" 2>/dev/null)" = "$PT" ] || bad "second ciphertext did not decrypt"

# ---------------------------------------------------------------------------
# [6c] the negative control for [6]. A leg that cannot fail is decoration, so
#      build a COPY whose OAEP seed is a constant and require that copy to
#      produce the identical ciphertext twice. If this control does not fire,
#      [6] is measuring nothing.
# ---------------------------------------------------------------------------
mkdir -p "$T/ctl"
sed 's/seed := crypto.random_hex(hlen())/seed := "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"/' \
    tools/tycho-rsa/main.ty > "$T/ctl/main.ty"
if cmp -s "$T/ctl/main.ty" tools/tycho-rsa/main.ty; then
    bad "control: the seed line was not found in main.ty -- the control patched nothing"
elif ! "$TYCHOC" "$T/ctl/main.ty" -o "$T/ctlbin" >"$T/ctl.log" 2>&1; then
    bad "control: fixed-seed copy did not compile"; sed 's/^/      /' "$T/ctl.log"
else
    ca=$("$T/ctlbin" encrypt "$PT" 65537 "$N" 2>/dev/null)
    cb=$("$T/ctlbin" encrypt "$PT" 65537 "$N" 2>/dev/null)
    if [ "$ca" != "$cb" ]; then
        bad "control: a FIXED OAEP seed still produced two different ciphertexts -- leg [6] cannot fail"
    fi
fi

# ---------------------------------------------------------------------------
# [7] raw RSA is off the command line, and the padding's own rejections hold.
# ---------------------------------------------------------------------------
if "$RSA" modexp 2 3 5 >/dev/null 2>&1; then
    bad "the 'modexp' command still exists -- unpadded RSA is reachable from the CLI"
fi
grep -q '"modexp"' tools/tycho-rsa/main.ty && bad "main.ty still dispatches a 'modexp' command"

# a ciphertext with one flipped byte must be refused, not silently decoded
tam="$(printf %s "$c1" | cut -c1-254)$(printf %02x $(( 0x$(printf %s "$c1" | cut -c255-256) ^ 1 )))"
[ "$tam" != "$c1" ] || bad "the tamper probe did not actually change the ciphertext"
if "$RSA" decrypt "$tam" "$D" "$N" >/dev/null 2>&1; then
    bad "a tampered ciphertext decrypted successfully -- the OAEP integrity check is not running"
fi
# a short ciphertext, an over-long message, and a non-hex message
"$RSA" decrypt "00ff" "$D" "$N" >/dev/null 2>&1 && bad "a 2-byte ciphertext was accepted"
long=$(printf 'aa%.0s' $(seq 1 63))     # 63 bytes > k - 2*32 - 2 = 62
"$RSA" encrypt "$long" 65537 "$N" >/dev/null 2>&1 && bad "a 63-byte message was accepted (the OAEP limit is 62)"
"$RSA" encrypt "zz" 65537 "$N" >/dev/null 2>&1 && bad "a non-hex message was accepted"
"$RSA" encrypt "$PT" 65537 "12x34" >/dev/null 2>&1 && bad "a non-decimal modulus was accepted"

if [ "$fail" -eq 0 ]; then
    echo "tycho-rsa: green (textbook vector, 3 python-pow modexp sizes, Miller-Rabin probes incl. Carmichael 561, CSPRNG keygen invariants == golden; RSAES-OAEP round trip through the CLI; two 512-bit keygens differ; OAEP known-answer test vs python cryptography BOTH ways; the same plaintext encrypts twice to different ciphertexts, with a fixed-seed copy as the control; no modexp command; tampered/short/over-long/non-hex inputs all refused)"
else
    echo "tycho-rsa: FAIL"; exit 1
fi
