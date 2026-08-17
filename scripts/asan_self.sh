set -u
cd "$(dirname "$0")/.." || exit 2

CC="${CC:-cc}"
SAN="build/tychoc-asan"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# See the LD_PRELOAD note above: a foreign preload in the launching shell breaks
# every ASan binary before main(). Report it rather than papering over it.
if [ -n "${LD_PRELOAD:-}" ]; then
    echo "asan-self: NOTE — unsetting a foreign LD_PRELOAD='$LD_PRELOAD' for the sanitized child"
    echo "asan-self:        processes (it would load before libasan and abort them at startup)."
    unset LD_PRELOAD
fi
export ASAN_OPTIONS=detect_leaks=0
export UBSAN_OPTIONS=print_stacktrace=1

echo "asan-self: building $SAN  (ASan+UBSan, -fno-sanitize-recover=all)"
mkdir -p build
if ! $CC -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1 -fwrapv \
        -std=c11 -Ibuild src/tychoc.c -o "$SAN" 2>"$TMP/build.log"; then
    echo "asan-self: FATAL — could not build the sanitized compiler"
    sed 's/^/      /' "$TMP/build.log"
    exit 2
fi

pass=0
fail=0
fails=""

# check_one <entry.ty> <label>
check_one() {
    hi="$1"; name="$2"
    "$SAN" "$hi" --emit-c -o "$TMP/out" >"$TMP/log" 2>&1
    rc=$?
    # The sanitizer's own voice. tychoc's diagnostics are `file:LINE: error: MSG`
    # (lowercase), so they cannot collide with `ERROR: ` here; the only
    # "runtime error" string in src/tychoc.c is a comment (:8320), never output.
    if grep -qE 'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:|ERROR: ' "$TMP/log"; then
        echo "FAIL  $name  (sanitizer report)"
        sed 's/^/      /' "$TMP/log"
        fail=$((fail + 1)); fails="$fails $name"
    elif [ "$rc" -ge 128 ]; then
        # Killed by a signal. A rejected program exits 1 with a diagnostic; a
        # SIGSEGV/SIGABRT is the compiler falling over, which is a finding even
        # when the sanitizer did not get a word in first.
        echo "FAIL  $name  (compiler died on signal, rc=$rc)"
        sed 's/^/      /' "$TMP/log"
        fail=$((fail + 1)); fails="$fails $name"
    else
        pass=$((pass + 1))
    fi
    rm -f "$TMP/out.c"
}

for hi in examples/*.ty tests/*.ty tests/conc/*.ty \
          tests/reject/*.ty tests/abort/*.ty tests/diag/*.ty tests/warn/*.ty \
          compiler/tychoc0.ty tools/tycho.ty tools/tychofmt.ty tools/lsp.ty; do
    [ -e "$hi" ] || continue
    check_one "$hi" "$hi"
done
for d in tests/pkg/*/ tests/reject/pkg/*/; do
    [ -d "$d" ] || continue
    [ -f "${d}main.ty" ] || continue
    check_one "${d}main.ty" "${d}main.ty"
done

echo "-----------------------------------------"
echo "asan-self: compiled: $pass   failed: $fail"
[ "$fail" -eq 0 ] || { echo "failed:$fails"; exit 1; }
echo "asan-self: all green (tychoc's own execution is ASan+UBSan clean over the corpus)"
