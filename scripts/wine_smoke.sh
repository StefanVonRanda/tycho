set -u
cd "$(dirname "$0")/.." || exit 2
MINGWCC="$(command -v x86_64-w64-mingw32-gcc || true)"
WINE="$(command -v wine64 || command -v wine || true)"
[ -n "$WINE" ] || { echo "SKIP: neither wine64 nor wine on PATH -- windows Wine smoke skipped"; exit 0; }
[ -n "$MINGWCC" ] || { echo "SKIP: x86_64-w64-mingw32-gcc not on PATH -- windows Wine smoke skipped"; exit 0; }
export LD_PRELOAD=                       # the tmux block-nnp.so shim breaks wine

T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0

# the mingw cross-compiled compiler (build dir; the repo .gitignore covers it)
mkdir -p build
make -s build/tycho_rt_embed.h
if [ ! -x build/tychoc-mingw.exe ] \
   || [ src/tychoc.c -nt build/tychoc-mingw.exe ] \
   || [ build/tycho_rt_embed.h -nt build/tychoc-mingw.exe ]; then
    "$MINGWCC" -O2 -fwrapv -std=c11 -Ibuild src/tychoc.c -o build/tychoc-mingw.exe \
        || { echo "FAIL: mingw compiler build"; exit 2; }
fi
WINEDLL='Z:\usr\x86_64-w64-mingw32\lib'   # libwinpthread-1.dll lives here on Debian
W="env -u LD_PRELOAD WINEDEBUG=-all WINEPATH=$WINEDLL $WINE"

# cross-compile fixture $1.ty -> $T/$1.exe; run under wine; diff vs $2
smoke() {
    name="$1"; src="$2"; golden="$3"
    env -u LD_PRELOAD WINEDEBUG=-all $WINE ./build/tychoc-mingw.exe "$src" --emit-c -o "$T/$name" >/dev/null 2>&1 \
        || { echo "FAIL $name (emit)"; fail=1; return; }
    "$MINGWCC" -O3 -fwrapv -pthread -o "$T/$name.exe" "$T/$name.c" -lm 2>/dev/null \
        || { echo "FAIL $name (cc)"; fail=1; return; }
    if $W "$T/$name.exe" 2>/dev/null | diff -q - "$golden" >/dev/null 2>&1; then
        echo "ok    $name (byte-identical under Wine)"
    else
        echo "FAIL $name (output differs)"; fail=1
    fi
}

echo ">>> concurrency, timers, floats, io (byte-identical vs goldens)"
for f in basic chan chancap1 workers parfor select select_parfor implicit; do
    smoke "conc-$f" "tests/conc/$f.ty" "tests/conc/$f.out"
done
smoke clock     tests/clock.ty     tests/clock.out
smoke floats    tests/floats.ty    tests/floats.out
smoke iobuiltins tests/iobuiltins.ty tests/iobuiltins.out

echo ">>> runtime stack-overflow guard (deep recursion fails closed, under Wine)"
printf 'fn f(n: int) -> int:\n    if n <= 0:\n        return 1\n    return n + f(n - 1)\nfn main():\n    print(str(f(2000000)))\n' > "$T/prog_big.ty"
printf 'fn f(n: int) -> int:\n    if n <= 0:\n        return 0\n    return f(n - 1)\nfn main():\n    print(str(f(2000000)))\n' > "$T/prog_small.ty"
printf 'fn deep(n: int) -> int:\n    if n <= 0:\n        return 0\n    return deep(n - 1)\nfn tm() -> int:\n    return deep(2000000)\nfn main():\n    t := spawn tm()\n    print(str(t.wait()))\n' > "$T/prog_spawn.ty"
printf 'fn f(n: int) -> int:\n    if n <= 0:\n        return 1\n    return n + f(n - 1)\nfn main():\n    print(str(f(1000)))\n' > "$T/prog_ok_big.ty"
printf 'fn deep(n: int) -> int:\n    if n <= 0:\n        return 0\n    return deep(n - 1)\nfn tm() -> int:\n    return deep(1000)\nfn main():\n    t := spawn tm()\n    print(str(t.wait()))\n' > "$T/prog_ok_spawn.ty"
for n in prog_big prog_small prog_spawn; do
    env -u LD_PRELOAD WINEDEBUG=-all $WINE ./build/tychoc-mingw.exe "$T/$n.ty" --emit-c -o "$T/$n" >/dev/null 2>&1
    "$MINGWCC" -O3 -fwrapv -pthread -o "$T/$n.exe" "$T/$n.c" -lm 2>/dev/null
    $W "$T/$n.exe" >"$T/$n.out" 2>"$T/$n.err"; rc=$?
    if [ "$rc" -eq 0 ] || [ "$rc" -ge 128 ]; then echo "FAIL $n (exit $rc -- must fail closed 1-127)"; fail=1
    elif [ -s "$T/$n.out" ]; then echo "FAIL $n (stdout not empty)"; fail=1
    elif grep -q "stack overflow" "$T/$n.err"; then echo "ok    $n (died cleanly, rc=$rc, under Wine)"
    else echo "FAIL $n (no diagnostic on stderr)"; fail=1; fi
done
for n in prog_ok_big prog_ok_spawn; do
    env -u LD_PRELOAD WINEDEBUG=-all $WINE ./build/tychoc-mingw.exe "$T/$n.ty" --emit-c -o "$T/$n" >/dev/null 2>&1
    "$MINGWCC" -O3 -fwrapv -pthread -o "$T/$n.exe" "$T/$n.c" -lm 2>/dev/null
    out=$($W "$T/$n.exe" 2>/dev/null); rc=$?
    [ "$rc" -eq 0 ] && [ -n "$out" ] && echo "ok    $n (ran, printed $out)" || { echo "FAIL $n (rc=$rc out='$out')"; fail=1; }
done

[ "$fail" -eq 0 ] && echo "wine-smoke: all green (approximation only -- the definitive pass is the windows CI leg)" || { echo "wine-smoke: FAIL"; exit 1; }
