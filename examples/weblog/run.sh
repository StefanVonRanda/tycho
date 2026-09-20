set -u
cd "$(dirname "$0")/../.." || exit 2                  # repo root
TYCHOC="${TYCHOC:-./tychoc1}"
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
CC="${CC:-cc}"
export TYCHO_CORELIB="$PWD/corelib"
D=examples/weblog
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT

# tychoc (C reference compiler)
$TYCHOC "$D/main.ty" -o "$T/wl_c" 2>"$T/err" || { echo "weblog: tychoc BUILD FAILED"; cat "$T/err"; exit 1; }
"$T/wl_c" > "$T/out_c"                                # no args -> embedded demo log

if [ "${RECORD:-0}" = "1" ]; then
    cp "$T/out_c" "$D/expected.out"; echo "weblog: golden recorded ($D/expected.out)"; exit 0
fi

fail=0
diff -u "$D/expected.out" "$T/out_c" || { echo "weblog: tychoc output differs from golden"; fail=1; }

# A MALFORMED BYTE COUNT MUST FAIL THE RECORD, and the shipped access.log has
# none -- so the golden above passes whether or not the field is checked, which
# is how `strings.parse_int` (fail-open: "50x" is 50, an overflow is 0) sat on an
# attacker-influenced field unnoticed (FRICTION 115). This leg supplies what the
# corpus lacks. Without the fix /over and /junk are ACCEPTED carrying 0 and 50.
cat > "$T/bad.log" <<'LOG'
1.2.3.4 - - [10/Oct/2000:13:55:36 -0700] "GET /good HTTP/1.0" 200 100
1.2.3.4 - - [10/Oct/2000:13:55:37 -0700] "GET /over HTTP/1.0" 200 9999999999999999999
1.2.3.4 - - [10/Oct/2000:13:55:38 -0700] "GET /junk HTTP/1.0" 200 50x
1.2.3.4 - - [10/Oct/2000:13:55:39 -0700] "GET /nobody HTTP/1.0" 304 -
LOG
"$T/wl_c" "$T/bad.log" > "$T/bad.out" 2>&1
for u in /over /junk; do
    grep -q " $u\$" "$T/bad.out" && { echo "weblog: $u was ACCEPTED -- a malformed byte count did not fail the record"; fail=1; }
done
grep -q " /good\$" "$T/bad.out" || { echo "weblog: /good was rejected -- the check is refusing valid records"; fail=1; }
# CLF spells "no body" as `-`; that is not malformed and must still count, as 0.
grep -qE "^ +1 +0 +/nobody\$" "$T/bad.out" || { echo "weblog: /nobody (bytes '-') was not kept as 0 bytes"; fail=1; sed -n "/hits    bytes/,+5p" "$T/bad.out"; }

[ $fail -eq 0 ] && echo "weblog: ok (tychoc == golden; a malformed byte count fails the record, '-' still counts as 0)" || exit 1
