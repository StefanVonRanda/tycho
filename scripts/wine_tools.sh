#!/bin/sh
# Phase-5 wine-tools: every tools/ tool cross-compiled under mingw (the
# Windows build proof), plus representative runs under Wine for the
# deterministic ones, compared against their own lane goldens where cheap.
# The Linux-box approximation of "make tools + the tool lanes on the Windows
# box" (plan_windows.md phase 5).
#
#   sh scripts/wine_tools.sh
#   WINE_TOOLS_FILTER=ar sh scripts/wine_tools.sh
#
# WHAT IT ASSERTS: (a) every tool compiles AND links for Windows -- tycho-ar
# (-lz), tycho-kvsrv (-lws2_32), tycho-debug (its shim); tycho-fetch is the
# one SKIP (mingw libcurl lives in MSYS2). (b) the deterministic tools run
# correctly under Wine: tycho-q answers a query, tycho-scheme runs fib,
# tycho-ar creates and lists an archive. The FULL lane fixture suites are
# phase-6 harness territory; their definitive pass is the windows CI leg.
# NOT a gate, NOT a Windows verdict. Skips loudly when mingw/wine absent.
set -u
cd "$(dirname "$0")/.." || exit 2
MINGWCC="$(command -v x86_64-w64-mingw32-gcc || true)"
command -v wine64 >/dev/null 2>&1 || { echo "SKIP: wine64 not on PATH"; exit 0; }
[ -n "$MINGWCC" ] || { echo "SKIP: x86_64-w64-mingw32-gcc not on PATH"; exit 0; }
export LD_PRELOAD=
FILTER="${WINE_TOOLS_FILTER:-}"
T="$(mktemp -d)"; trap 'rm -rf "$T"; pkill -f "wine64.*/tmp/tmp\." 2>/dev/null; pkill wineserver 2>/dev/null' EXIT
fail=0; built=0; skipped=0

mkdir -p build
make -s build/tycho_rt_embed.h
if [ ! -x build/tychoc-mingw.exe ]; then
    "$MINGWCC" -O2 -fwrapv -std=c11 -Ibuild src/tychoc.c -o build/tychoc-mingw.exe \
        || { echo "FAIL: mingw compiler build"; exit 2; }
fi
CORELIB="Z:\\$(pwd | sed 's|^/||; s|/|\\|g')\\corelib"
E="env -u LD_PRELOAD WINEDEBUG=-all TYCHO_CORELIB=$CORELIB wine64 ./build/tychoc-mingw.exe"
W="env -u LD_PRELOAD WINEDEBUG=-all WINEPATH=Z:\\\\usr\\\\x86_64-w64-mingw32\\\\lib wine64"

# <name> <entry> <extra-shim|-> <extra-libs|->  ; fetch is a documented skip
build_tool() {
    name="$1"; entry="$2"; shim="$3"; libs="$4"
    case "$name" in *"$FILTER"*) ;; *) return ;; esac
    S=""; [ "$shim" != "-" ] && S="--shim $shim"
    LIBS=""; [ "$libs" != "-" ] && LIBS="$libs"
    $E "$entry" $S --emit-c -o "$T/$name" >/dev/null 2>&1 || { echo "FAIL $name (emit)"; fail=$((fail+1)); return; }
    SHIMS=$($E "$entry" $S --print-shims 2>/dev/null | sed 's|^Z:||; s|\\|/|g' | tr '\n' ' ')
    [ "$shim" != "-" ] && SHIMS="$SHIMS $shim"
    if "$MINGWCC" -O3 -fwrapv -pthread -o "$T/$name.exe" "$T/$name.c" $SHIMS $LIBS -lm 2>"$T/$name.cc"; then
        echo "ok    $name (built for Windows)"; built=$((built+1))
    else
        echo "FAIL $name (link: $(grep -oE 'undefined reference to .[^'\'']*' "$T/$name.cc" | head -1))"
        fail=$((fail+1))
    fi
}

echo ">>> tools cross-compiled under mingw (the Windows build proof)"
build_tool tycho-ar    tools/tycho-ar/main.ty       -        -lz
build_tool tycho-q     tools/tycho-q/main.ty        -        -
build_tool tycho-vm    tools/tycho-vm/main.ty       -        -
build_tool tycho-scheme tools/tycho-scheme/main.ty  -        -
build_tool tycho-kv    tools/tycho-kv/main.ty       -        -
build_tool tycho-chess tools/tycho-chess/main.ty    -        -
build_tool tycho-rsa   tools/tycho-rsa/main.ty      -        -
build_tool tycho-sat   tools/tycho-sat/main.ty      -        -
build_tool tycho-kvsrv tools/tycho-kvsrv/main.ty    -        -lws2_32
build_tool tycho-build tools/tycho-build/main.ty    -        -
build_tool tycho-debug tools/tycho-debug/main.ty    tools/tycho-debug/debug_shim.c -
build_tool tycho       tools/tycho.ty               tools/tycho_shim.c -
build_tool tychofmt    tools/tychofmt.ty            -        -
build_tool lsp         tools/lsp.ty                 tools/lsp_shim.c -
echo "skip tycho-fetch (mingw libcurl is MSYS2-only; the shim code is portable)"; skipped=$((skipped+1))

echo ">>> deterministic tools run under Wine"
# tycho-q: a query over a CSV
case "q" in *"$FILTER"*) printf 'a,b\n1,2\n3,4\n' > "$T/q.csv"
    out=$($W "$T/tycho-q.exe" "select * from \"$T/q.csv\"" 2>/dev/null)
    if [ "$out" = "a,b
1,2
3,4" ]; then echo "ok    tycho-q (query under Wine)"; built=$((built+1))
    else echo "FAIL tycho-q (got: $out)"; fail=$((fail+1)); fi ;; esac
# tycho-scheme: run fib
case "scheme" in *"$FILTER"*) out=$($W "$T/tycho-scheme.exe" run tools/tycho-scheme/progs/fib.scm 2>/dev/null)
    if echo "$out" | grep -q "6765"; then echo "ok    tycho-scheme (fib under Wine)"; built=$((built+1))
    else echo "FAIL tycho-scheme (got: $out)"; fail=$((fail+1)); fi ;; esac
# tycho-ar: create + list a deterministic archive
case "ar" in *"$FILTER"*) mkdir -p "$T/ardir"; printf 'hello\n' > "$T/ardir/a.txt"; printf 'world\n' > "$T/ardir/b.txt"
    $W "$T/tycho-ar.exe" c "$T/test.ar" "$T/ardir" >/dev/null 2>&1
    out=$($W "$T/tycho-ar.exe" t "$T/test.ar" 2>/dev/null)
    if echo "$out" | grep -q "a.txt" && echo "$out" | grep -q "b.txt"; then echo "ok    tycho-ar (create+list under Wine)"; built=$((built+1))
    else echo "FAIL tycho-ar (got: $out)"; fail=$((fail+1)); fi ;; esac

echo
echo "wine-tools: built+ran $built  failed $fail  skipped $skipped"
[ "$fail" -eq 0 ] || { echo "wine-tools: FAIL ($fail)"; exit 1; }
echo "wine-tools: all green (approximation only)"
