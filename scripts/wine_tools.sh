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
# Wine 9.0 merged wine64 into a single 64-bit `wine`, and Arch/CachyOS ships
# wine 11.x with no wine64 binary at all -- so a bare `command -v wine64` made
# this lane SKIP on every modern Wine while printing a reason that read like a
# missing install. Prefer wine64 when it exists (older split installs), else
# wine. Measured 2026-08-09 on this box: wine-11.14, no wine64.
WINE="$(command -v wine64 || command -v wine || true)"
[ -n "$WINE" ] || { echo "SKIP: neither wine64 nor wine on PATH"; exit 0; }
[ -n "$MINGWCC" ] || { echo "SKIP: x86_64-w64-mingw32-gcc not on PATH"; exit 0; }
export LD_PRELOAD=
FILTER="${WINE_TOOLS_FILTER:-}"
T="$(mktemp -d)"; trap 'rm -rf "$T"; pkill -f "wine.*/tmp/tmp\." 2>/dev/null; pkill wineserver 2>/dev/null' EXIT
fail=0; built=0; skipped=0

mkdir -p build
make -s build/tycho_rt_embed.h
# REBUILD WHEN THE SOURCE MOVED, not merely when the exe is absent. Until
# 2026-08-13 this said `if [ ! -x ... ]`, so an exe cross-built once was reused
# for ever: on this box it was 8 days old and 25 fixtures "failed" at emit/cc
# under mingw purely because the compiler predated the features they use. A lane
# whose subject is stale reports on a program nobody is running.
if [ ! -x build/tychoc-mingw.exe ] \
   || [ src/tychoc.c -nt build/tychoc-mingw.exe ] \
   || [ build/tycho_rt_embed.h -nt build/tychoc-mingw.exe ]; then
    "$MINGWCC" -O2 -fwrapv -std=c11 -Ibuild src/tychoc.c -o build/tychoc-mingw.exe \
        || { echo "FAIL: mingw compiler build"; exit 2; }
fi
CORELIB="Z:\\$(pwd | sed 's|^/||; s|/|\\|g')\\corelib"
E="env -u LD_PRELOAD WINEDEBUG=-all TYCHO_CORELIB=$CORELIB $WINE ./build/tychoc-mingw.exe"
W="env -u LD_PRELOAD WINEDEBUG=-all WINEPATH=Z:\\\\usr\\\\x86_64-w64-mingw32\\\\lib $WINE"

# Can the CROSS toolchain link -l<name>? A tool whose library is not installed
# for mingw is an ENVIRONMENT gap, not a port failure, and wine_corelib.sh has
# always said so by name ("skip <reason>"); this lane had no such notion and
# would report a missing cross library as `FAIL <tool> (link: undefined reference
# to ...)`, which reads like the port cannot build the tool.
#
# THIS BOX IS NOT SUCH A HOST, and the probe finds nothing to skip here: mingw
# zlib IS installed, `-lz` links, and tycho-ar builds and runs under Wine
# (measured 2026-08-13). The net is EXERCISED anyway, two ways, so it cannot rot
# into an always-yes: a self-check leg below asks it for a library that cannot
# exist and requires a NO (breaking the probe to `return 0` reddens that leg,
# measured 2026-08-14), and forcing an impossible `-l` onto a real tool produces
# `skipped 3, failed 0` -- build skipped, run skipped, nothing counted wrong.
# Its two failures earlier that day were the stale
# cross-compiler fixed in 94aa75dd, NOT a missing library -- the `pkg-config
# could not resolve dependency 'zlib'` line the emit prints is about the MINGW
# pkg-config path and does not stop the link. So this is a safety net for a host
# without the library, kept because the alternative reading of that FAIL cost an
# hour today. Answer cached per lib; the probe is a compile, not a path guess.
have_mingw_lib() {
    _l="${1#-l}"
    eval "_c=\${_have_$_l:-}"
    if [ -z "$_c" ]; then
        printf 'int main(void){return 0;}\n' > "$T/_probe.c"
        if "$MINGWCC" "$T/_probe.c" "-l$_l" -o "$T/_probe.exe" >/dev/null 2>&1; then _c=yes; else _c=no; fi
        eval "_have_$_l=\$_c"
    fi
    [ "$_c" = yes ]
}

# <name> <entry> <extra-shim|-> <extra-libs|->  ; fetch is a documented skip
build_tool() {
    name="$1"; entry="$2"; shim="$3"; libs="$4"
    case "$name" in *"$FILTER"*) ;; *) return ;; esac
    if [ "$libs" != "-" ]; then
        for _lib in $libs; do
            case "$_lib" in
                -l*) have_mingw_lib "$_lib" || {
                        echo "skip  $name (${_lib#-l} not installed for mingw on this host)"
                        skipped=$((skipped+1)); return; } ;;
            esac
        done
    fi
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

# SELF-CHECK: the probe above must be able to say NO. It skipped nothing on the
# host that introduced it -- mingw zlib was installed -- so the net sat
# unexercised and a `have_mingw_lib` that always answered yes would have looked
# identical (its own `gap:` said so). A library that cannot exist is
# host-independent, so this leg runs everywhere and costs one failed cc.
if have_mingw_lib -lnosuchlib_tycho_probe; then
    echo "FAIL self-check: have_mingw_lib said YES for a library that cannot exist"
    fail=$((fail+1))
else
    echo "ok    self-check (have_mingw_lib refuses an absent library)"
fi

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
# A run leg needs the exe its build stage produced. When that stage SKIPPED --
# a cross library this host does not have -- the exe is absent and the run was
# counted as a FAILURE: that is why a missing library read as TWO failures for
# one tool (tycho-ar, 2026-08-13). A skipped build is a skipped run.
have_exe() {
    [ -x "$T/$1.exe" ] && return 0
    echo "skip  $1 (its build was skipped -- no exe to run)"
    skipped=$((skipped+1)); return 1
}
# tycho-q: a query over a CSV
case "q" in *"$FILTER"*) have_exe tycho-q && { printf 'a,b\n1,2\n3,4\n' > "$T/q.csv"
    out=$($W "$T/tycho-q.exe" "select * from \"$T/q.csv\"" 2>/dev/null)
    if [ "$out" = "a,b
1,2
3,4" ]; then echo "ok    tycho-q (query under Wine)"; built=$((built+1))
    else echo "FAIL tycho-q (got: $out)"; fail=$((fail+1)); fi ; } ;; esac
# tycho-scheme: run fib
case "scheme" in *"$FILTER"*) have_exe tycho-scheme && { out=$($W "$T/tycho-scheme.exe" run tools/tycho-scheme/progs/fib.scm 2>/dev/null)
    if echo "$out" | grep -q "6765"; then echo "ok    tycho-scheme (fib under Wine)"; built=$((built+1))
    else echo "FAIL tycho-scheme (got: $out)"; fail=$((fail+1)); fi ; } ;; esac
# tycho-ar: create + list a deterministic archive
case "ar" in *"$FILTER"*) have_exe tycho-ar && { mkdir -p "$T/ardir"; printf 'hello\n' > "$T/ardir/a.txt"; printf 'world\n' > "$T/ardir/b.txt"
    $W "$T/tycho-ar.exe" c "$T/test.ar" "$T/ardir" >/dev/null 2>&1
    out=$($W "$T/tycho-ar.exe" t "$T/test.ar" 2>/dev/null)
    if echo "$out" | grep -q "a.txt" && echo "$out" | grep -q "b.txt"; then echo "ok    tycho-ar (create+list under Wine)"; built=$((built+1))
    else echo "FAIL tycho-ar (got: $out)"; fail=$((fail+1)); fi ; } ;; esac

echo
echo "wine-tools: built+ran $built  failed $fail  skipped $skipped"
[ "$fail" -eq 0 ] || { echo "wine-tools: FAIL ($fail)"; exit 1; }
echo "wine-tools: all green (approximation only)"
