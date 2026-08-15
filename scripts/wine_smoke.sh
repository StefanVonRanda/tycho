#!/bin/sh
# Wine smoke for the native-Windows port (docs/internals/windows-port.md). Cross-compiles
# selected fixtures with the mingw-w64 compiler and runs them under Wine,
# comparing output byte-for-byte against the Linux goldens.
#
#   sh scripts/wine_smoke.sh
#
# WHAT IT IS: the Linux-box inner loop of the windows port. The compiler
# (build/tychoc-mingw.exe) is built with the mingw-w64 CROSS compiler; each
# fixture's emitted C is compiled with the SAME mingw gcc the transpiler would
# invoke on a Windows host; the result runs under Wine.
#
# WHAT IT IS NOT: a gate, and not a Windows verdict. Wine is an approximation
# of Win32 -- scheduling, exception and file semantics all differ in ways the
# fixtures can paper over. "Green under Wine" is recorded as exactly that; the
# DEFINITIVE pass is the windows-latest CI leg of docs/internals/windows-port.md phases 6/7.
# It SKIPS loudly when the mingw cross compiler or wine is absent.
#
# COVERAGE, and why these fixtures: the conc positives pin the concurrency
# model (spawn/wait, channels, backpressure, parallel-for reductions, select)
# byte-identically; clock pins the timer surface (winpthreads clock_gettime);
# floats pins the locale-fallback float formatting; iobuiltins pins list_dir
# (mingw dirent). The recursion set is the runtime stack-overflow guard: deep
# recursion must fail closed on the main thread AND in a spawned task (exit
# 1-127, empty stdout, "stack overflow" on stderr), and the modestly-nested
# controls must RUN -- the guard must not be trigger-happy. The fixtures are
# generated here, mirroring tests/recursion/run.sh's shapes.
set -u
cd "$(dirname "$0")/.." || exit 2
MINGWCC="$(command -v x86_64-w64-mingw32-gcc || true)"
# Wine 9.0 merged wine64 into a single 64-bit `wine`, and Arch/CachyOS ships
# wine 11.x with no wine64 binary at all -- so a bare `command -v wine64` made
# this lane SKIP on every modern Wine while printing a reason that read like a
# missing install. Prefer wine64 when it exists (older split installs), else
# wine. Measured 2026-08-09 on this box: wine-11.14, no wine64.
WINE="$(command -v wine64 || command -v wine || true)"
[ -n "$WINE" ] || { echo "SKIP: neither wine64 nor wine on PATH -- windows Wine smoke skipped"; exit 0; }
[ -n "$MINGWCC" ] || { echo "SKIP: x86_64-w64-mingw32-gcc not on PATH -- windows Wine smoke skipped"; exit 0; }
export LD_PRELOAD=                       # the tmux block-nnp.so shim breaks wine

T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0

# the mingw cross-compiled compiler (build dir; the repo .gitignore covers it)
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
# fixtures mirror tests/recursion/run.sh's generated-code shapes
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
