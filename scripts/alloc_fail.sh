#!/bin/sh
# Targeted allocation-failure lane. Linux/glibc only.
#
# FI_MALLOC_SIZE=1 fails every one-byte malloc in the target process. The http
# shim used to do `r->body = malloc(1); r->body[0] = 0;` with no NULL check, so
# this lane caught a real SIGSEGV. The C control proves the injector still
# works; the http leg is the regression.
set -u
cd "$(dirname "$0")/.."
case "$(uname -s)" in
    Linux) ;;
    *) echo "alloc-fail: skip (LD_PRELOAD injector is Linux-only)"; exit 0 ;;
esac
CC="${CC:-cc}"
TYCHOC="${TYCHOC:-./tychoc1}"
[ -x "$TYCHOC" ] || { echo "alloc-fail: no $TYCHOC -- run make" >&2; exit 2; }
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
SO="$T/alloc_inject.so"
$CC -shared -fPIC -O2 -Wall -Wextra -Werror -o "$SO" scripts/alloc_inject.c -ldl || exit 2
export TYCHO_CORELIB="$PWD/corelib"

cat > "$T/control.c" <<'EOF'
#include <stdlib.h>
int main(void) { char *p = malloc(1); return p ? 0 : 42; }
EOF
$CC -O0 -o "$T/control" "$T/control.c" || exit 2
timeout 10 env FI_MALLOC_SIZE=1 LD_PRELOAD="$SO" "$T/control" >/dev/null 2>&1
rc=$?
if [ "$rc" -ne 42 ]; then
    echo "FAIL control: FI_MALLOC_SIZE=1 did not fail malloc(1) (rc=$rc)"
    exit 1
fi
echo "ok   control (a one-byte malloc is injectable)"

if ! "$TYCHOC" corelib/test/http/main.ty -o "$T/http" >"$T/http.build" 2>&1; then
    echo "skip http (build: $(head -1 "$T/http.build"))"
    exit 0
fi
timeout 30 env FI_MALLOC_SIZE=1 LD_PRELOAD="$SO" "$T/http" >"$T/http.out" 2>"$T/http.err"
rc=$?
if [ "$rc" -eq 139 ] || [ "$rc" -ge 128 ] && [ "$rc" -ne 124 ]; then
    echo "FAIL http: FI_MALLOC_SIZE=1 killed it (rc=$rc)"
    head -3 "$T/http.err" | sed 's/^/      /'
    exit 1
fi
if [ "$rc" -ne 0 ]; then
    echo "FAIL http: FI_MALLOC_SIZE=1 exited $rc"
    head -3 "$T/http.err" | sed 's/^/      /'
    exit 1
fi
echo "ok   http (1-byte allocation failure returns a null handle, no crash)"

# A small sweep over the allocation-ordinal mode on the same fixtures. Only a
# signal is a failure: malloc failure inside libc/libcurl can legitimately abort
# or hang, but it must not produce a segfault through an unchecked pointer.
for t in http crypto compress os io sqlite; do
    "$TYCHOC" "corelib/test/$t/main.ty" -o "$T/$t" >"$T/$t.build" 2>&1 || {
        echo "skip $t (build failed)"; continue
    }
    bad=0
    for n in 1 2 3 4 5; do
        timeout 2 env FI_MALLOC_FAIL="$n" LD_PRELOAD="$SO" "$T/$t" >/dev/null 2>&1
        rc=$?
        case "$rc" in
            124|0|1|2) ;;
            *)
                if [ "$rc" -ge 128 ] && [ "$rc" -le 191 ]; then
                    echo "FAIL $t FI_MALLOC_FAIL=$n signal rc=$rc"
                    bad=1
                fi ;;
        esac
    done
    [ "$bad" -eq 0 ] && echo "ok   $t (ordinal allocation failures do not crash)"
done

echo "alloc-fail: all green"
