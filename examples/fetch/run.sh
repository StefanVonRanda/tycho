set -u
cd "$(dirname "$0")/../.." || exit 2          # repo root
TYCHOC="${TYCHOC:-./tychoc}"
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
export TYCHO_CORELIB="$PWD/corelib"
CC="${CC:-cc}"
DEPS="$("$TYCHOC" examples/fetch/main.ty --print-deps)" \
    || { echo "fetch: FAIL (tychoc --print-deps)"; exit 1; }
for pkg in $DEPS; do
    pkg-config --exists "$pkg" 2>/dev/null || { echo "fetch: SKIP ($pkg not installed)"; exit 0; }
done
DEPF=""
[ -n "$DEPS" ] && DEPF="$(pkg-config --cflags --libs $DEPS)"
SHIM="$("$TYCHOC" examples/fetch/main.ty --print-shims)" \
    || { echo "fetch: FAIL (tychoc --print-shims)"; exit 1; }
RECORD="${RECORD:-0}"
golden=examples/fetch/expected.out
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
# mingw gcc ships no sanitizer runtime -- see the SKIP at the sanitizer leg below
case "$(uname -s)" in *MSYS*|*MINGW*|*CYGWIN*) IS_WINDOWS=1 ;; *) IS_WINDOWS=0 ;; esac
# The fixture is fetched over file://, so the URL needs a path the NATIVE program
# can open. Under MSYS2 $PWD is the POSIX view (/c/tycho), which libcurl -- a
# native Windows DLL -- cannot resolve: the request came back "no response" and
# the lane read as a core:http failure. cygpath -m gives the mixed form
# (C:/tycho) that both a file: URL and the CRT accept.
if [ "$IS_WINDOWS" = 1 ]; then
    URL="file:///$(cygpath -m "$PWD")/examples/fetch/fixture.json"
else
    URL="file://$PWD/examples/fetch/fixture.json"
fi

# (1) C reference compiler (auto-discovers the core:http shim + deps)
if ! "$TYCHOC" examples/fetch/main.ty -o "$T/c" >"$T/c.log" 2>&1; then
    echo "FAIL: tychoc compile"; sed 's/^/      /' "$T/c.log"; fail=1
else
    "$T/c" "$URL" > "$T/c.out" 2>&1
fi

if ! "$TYCHOC" examples/fetch/main.ty --emit-c -o "$T/san_src" >"$T/emit.log" 2>&1; then
    echo "FAIL: tychoc --emit-c"; sed 's/^/      /' "$T/emit.log"; fail=1
elif [ "$IS_WINDOWS" = 1 ]; then
    echo "SKIP fetch ASan/UBSan leg (mingw gcc ships no sanitizer runtime -- no -lasan/-lubsan; docs/internals/windows-port.md phase 2)"
elif ! $CC -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1 "$T/san_src.c" $SHIM -o "$T/san" -lm $DEPF 2>"$T/san.log"; then
    echo "FAIL: sanitizer cc"; sed 's/^/      /' "$T/san.log"; fail=1
else
    ASAN_OPTIONS=detect_leaks=0 "$T/san" "$URL" > "$T/san.out" 2>"$T/san.err" || { echo "FAIL: sanitizer fault"; sed 's/^/      /' "$T/san.err"; fail=1; }
    if grep -qiE 'runtime error|Sanitizer|ERROR: ' "$T/san.err"; then echo "FAIL: sanitizer report"; sed 's/^/      /' "$T/san.err"; fail=1; fi
fi

if [ "$RECORD" = 1 ]; then
    if [ "$fail" -ne 0 ]; then echo "FAIL: refusing to RECORD from a failed run"; exit 1; fi
    cp "$T/c.out" "$golden"; echo "rec  fetch"
fi
if [ "$fail" -eq 0 ] && [ ! -f "$golden" ]; then echo "FAIL: no golden -- run RECORD=1"; fail=1; fi
if [ "$fail" -eq 0 ] && ! cmp -s "$T/c.out" "$golden"; then
    echo "FAIL: output != golden"; diff "$golden" "$T/c.out" | sed 's/^/      /'; fail=1
fi
if [ "$IS_WINDOWS" = 1 ]; then SAN="ASan SKIPPED (no mingw runtime)"; else SAN="ASan"; fi
[ "$fail" -eq 0 ] && echo "fetch: green (http+json+sha256+io+path compose; tychoc+$SAN; real libcurl via file://)" || { echo "fetch: FAIL"; exit 1; }
