set -u
cd "$(dirname "$0")/.." || exit 2
MINGWCC="$(command -v x86_64-w64-mingw32-gcc || true)"
WINE="$(command -v wine64 || command -v wine || true)"
[ -n "$WINE" ] || { echo "SKIP: neither wine64 nor wine on PATH"; exit 0; }
[ -n "$MINGWCC" ] || { echo "SKIP: x86_64-w64-mingw32-gcc not on PATH"; exit 0; }
export LD_PRELOAD=
FILTER="${WINE_TOOLS_FILTER:-}"
T="$(mktemp -d)"; trap 'rm -rf "$T"; pkill -f "wine.*/tmp/tmp\." 2>/dev/null; pkill wineserver 2>/dev/null' EXIT
fail=0; built=0; skipped=0

mkdir -p build
make -s build/tycho_rt_embed.h
if [ ! -x build/tychoc-mingw.exe ] \
   || [ src/tychoc.c -nt build/tychoc-mingw.exe ] \
   || [ build/tycho_rt_embed.h -nt build/tychoc-mingw.exe ]; then
    "$MINGWCC" -O2 -fwrapv -std=c11 -Ibuild src/tychoc.c -o build/tychoc-mingw.exe \
        || { echo "FAIL: mingw compiler build"; exit 2; }
fi
CORELIB="Z:\\$(pwd | sed 's|^/||; s|/|\\|g')\\corelib"
E="env -u LD_PRELOAD WINEDEBUG=-all TYCHO_CORELIB=$CORELIB $WINE ./build/tychoc-mingw.exe"
W="env -u LD_PRELOAD WINEDEBUG=-all WINEPATH=Z:\\\\usr\\\\x86_64-w64-mingw32\\\\lib $WINE"

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
