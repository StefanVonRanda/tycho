set -u
cd "$(dirname "$0")/../.." || exit 2          # repo root
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
export TYCHO_CORELIB="$PWD/corelib"
CC="${CC:-cc}"
RECORD="${RECORD:-0}"
golden=examples/site/expected.out
SRC=examples/site/main.ty
SITE=examples/site
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
mkdir -p "$T/out"
fail=0
# mingw gcc ships no sanitizer runtime -- see the SKIP at the ASan leg below
case "$(uname -s)" in *MSYS*|*MINGW*|*CYGWIN*) IS_WINDOWS=1 ;; *) IS_WINDOWS=0 ;; esac

shim="$("$TYCHOC" "$SRC" --print-shims)" \
    || { echo "site: FAIL (tychoc --print-shims)"; exit 1; }

# (1) C reference compiler
if ! "$TYCHOC" "$SRC" -o "$T/c" >"$T/c.log" 2>&1; then
    echo "FAIL: tychoc compile"; sed 's/^/      /' "$T/c.log"; fail=1
else
    "$T/c" "$SITE" "$T/out" > "$T/c.out" 2>&1
fi

# (2) ASan/UBSan over the emitted C
if ! "$TYCHOC" "$SRC" --emit-c -o "$T/e" >"$T/e.log" 2>&1; then
    echo "FAIL: tychoc --emit-c"; sed 's/^/      /' "$T/e.log"; fail=1
fi
if [ "$IS_WINDOWS" = 1 ]; then
    echo "SKIP site ASan/UBSan leg (mingw gcc ships no sanitizer runtime -- no -lasan/-lubsan; docs/internals/windows-port.md phase 2)"
elif ! $CC -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1 "$T/e.c" $shim -o "$T/san" -lm 2>"$T/san.log"; then
    echo "FAIL: sanitizer cc"; sed 's/^/      /' "$T/san.log"; fail=1
else
    ASAN_OPTIONS=detect_leaks=0 "$T/san" "$SITE" "$T/out" > "$T/san.out" 2>"$T/san.err" || { echo "FAIL: sanitizer fault"; sed 's/^/      /' "$T/san.err"; fail=1; }
    if grep -qiE 'runtime error|Sanitizer|ERROR: ' "$T/san.err"; then echo "FAIL: sanitizer report"; sed 's/^/      /' "$T/san.err"; fail=1; fi
fi

if [ "$RECORD" = 1 ]; then cp "$T/c.out" "$golden"; echo "rec  site"; fi
if [ "$fail" -eq 0 ] && [ ! -f "$golden" ]; then echo "FAIL: no golden -- run RECORD=1"; fail=1; fi
if [ "$fail" -eq 0 ] && ! cmp -s "$T/c.out" "$golden"; then
    echo "FAIL: output != golden"; diff "$golden" "$T/c.out" | sed 's/^/      /'; fail=1
fi
if [ "$IS_WINDOWS" = 1 ]; then SAN="ASan SKIPPED (no mingw runtime)"; else SAN="ASan"; fi
[ "$fail" -eq 0 ] && echo "site: green (io+path+json+csv+strings+sort+datetime+sha256 compose; tychoc+$SAN, matches golden)" || { echo "site: FAIL"; exit 1; }
