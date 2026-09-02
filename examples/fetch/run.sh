set -u
cd "$(dirname "$0")/../.." || exit 2          # repo root
TYCHOC="${TYCHOC:-./tychoc1}"
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
T="$(mktemp -d)"
srv=""
cleanup() { [ -n "$srv" ] && kill "$srv" 2>/dev/null; rm -rf "$T"; }
trap cleanup EXIT INT TERM
fail=0
# mingw gcc ships no sanitizer runtime -- see the SKIP at the sanitizer leg below
case "$(uname -s)" in *MSYS*|*MINGW*|*CYGWIN*) IS_WINDOWS=1 ;; *) IS_WINDOWS=0 ;; esac
# The fixture used to be fetched over file://, which needed a cygpath dance on
# Windows and, more to the point, stopped working when core:http grew its scheme
# fence -- http.get now refuses every scheme but http/https. So this lane serves
# the fixture over real loopback HTTP instead, which is closer to what the
# example is for. Port chosen by the kernel; readiness is a real connect, never
# a sleep. No python3 means no server, so the lane skips rather than lying.
command -v python3 >/dev/null 2>&1 || { echo "fetch: SKIP (no python3 to serve the fixture)"; exit 0; }
port=$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1]);s.close()')
python3 -m http.server "$port" --bind 127.0.0.1 --directory examples/fetch >/dev/null 2>&1 &
srv=$!
ready=0; i=0
while [ "$i" -lt 100 ]; do
    if python3 -c "
import socket,sys
s=socket.socket(); s.settimeout(0.2)
sys.exit(0 if s.connect_ex(('127.0.0.1',$port))==0 else 1)" 2>/dev/null; then ready=1; break; fi
    i=$((i + 1))
done
[ "$ready" -eq 1 ] || { echo "fetch: SKIP (the fixture server never came up)"; exit 0; }
URL="http://127.0.0.1:$port/fixture.json"

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
[ "$fail" -eq 0 ] && echo "fetch: green (http+json+sha256+io+path compose; tychoc+$SAN; real libcurl over loopback HTTP)" || { echo "fetch: FAIL"; exit 1; }
