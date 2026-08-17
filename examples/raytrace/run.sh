set -u
cd "$(dirname "$0")/../.." || exit 2
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
export TYCHO_CORELIB="$PWD/corelib"
CC="${CC:-cc}"
RECORD="${RECORD:-0}"
golden=examples/raytrace/expected.out
SRC=examples/raytrace/main.ty
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
# mingw gcc ships no sanitizer runtime; Apple ASan ships no LeakSanitizer.
case "$(uname -s)" in
    *MSYS*|*MINGW*|*CYGWIN*) IS_WINDOWS=1; TYCHO_LSAN=0 ;;
    Darwin) IS_WINDOWS=0; TYCHO_LSAN=0 ;;
    *) IS_WINDOWS=0; TYCHO_LSAN=1 ;;
esac

# (1) C reference compiler
if ! "$TYCHOC" "$SRC" -o "$T/c" >"$T/c.log" 2>&1; then
    echo "FAIL: tychoc compile"; sed 's/^/      /' "$T/c.log"; fail=1
else
    ( cd "$T" && ./c ) > "$T/c.out" 2>&1
fi

# (2) ASan/UBSan over the emitted C (the render must be leak-free too)
if [ "$IS_WINDOWS" = 1 ]; then
    echo "SKIP raytrace ASan/UBSan leg (mingw gcc ships no sanitizer runtime -- no -lasan/-lubsan; docs/internals/windows-port.md phase 2)"
elif ! { "$TYCHOC" "$SRC" --emit-c -o "$T/a" >/dev/null 2>&1 && \
       $CC -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1 "$T/a.c" -o "$T/asan" -lm 2>"$T/a.log"; }; then
    echo "FAIL: sanitizer cc"; sed 's/^/      /' "$T/a.log"; fail=1
else
    [ "$TYCHO_LSAN" = 1 ] || echo "SKIP raytrace LeakSanitizer (unavailable on macOS; ASan+UBSan still run)"
    ( cd "$T" && ASAN_OPTIONS=detect_leaks=$TYCHO_LSAN ./asan ) > "$T/asan.out" 2>"$T/asan.err" || { echo "FAIL: sanitizer fault"; sed 's/^/      /' "$T/asan.err"; fail=1; }
    if grep -qiE 'runtime error|Sanitizer|ERROR: ' "$T/asan.err"; then echo "FAIL: sanitizer report"; sed 's/^/      /' "$T/asan.err"; fail=1; fi
fi

# The emitted out.qoi must be a real QOI file (magic 'qoif' = 113 111 105 102).
if [ "$fail" -eq 0 ]; then
    magic=$(od -A n -t u1 -N 4 "$T/out.qoi" 2>/dev/null | awk '{$1=$1; print}')
    [ "$magic" = "113 111 105 102" ] || { echo "FAIL: out.qoi is not a QOI file (magic:$magic)"; fail=1; }
fi

if [ "$RECORD" = 1 ]; then cp "$T/c.out" "$golden"; echo "rec  raytrace"; fi
if [ "$fail" -eq 0 ] && [ ! -f "$golden" ]; then echo "FAIL: no golden -- run RECORD=1"; fail=1; fi
if [ "$fail" -eq 0 ] && ! cmp -s "$T/c.out" "$golden"; then
    echo "FAIL: output != golden"; diff "$golden" "$T/c.out" | sed 's/^/      /'; fail=1
fi
if [ "$IS_WINDOWS" = 1 ]; then SAN="ASan SKIPPED (no mingw runtime)"; else SAN="ASan"; fi
[ "$fail" -eq 0 ] && echo "raytrace: green (float-heavy Vec3 value semantics; tychoc + $SAN; valid QOI)" || { echo "raytrace: FAIL"; exit 1; }
