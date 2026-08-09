#!/bin/sh
# Phase-6 wine-ffi: the FFI lane's runnable legs (tests/ffi/run.sh),
# cross-compiled under mingw and run under Wine. Covers the golden
# (main.ty over a mingw-built libffidemo.a), the --shim leg, the
# package-scoped extern, the sized-int ABI values, and the compiler-side
# REJECTION legs (affine handle bans, shell-injection refusals, the
# inout-string out-param ban) driven through the MINGW compiler. The one
# leg that cannot run here is the ASan/UBSan recompile -- mingw ASan is
# experimental; that is the CI leg's job.
#
#   sh scripts/wine_ffi.sh
#   WINE_FFI_FILTER=shim sh scripts/wine_ffi.sh
#
# NOT a gate and NOT a Windows verdict. Skips loudly when mingw/wine absent.
set -u
cd "$(dirname "$0")/.." || exit 2
MINGWCC="$(command -v x86_64-w64-mingw32-gcc || true)"
MINGWAR="$(command -v x86_64-w64-mingw32-ar || true)"
# Wine 9.0 merged wine64 into a single 64-bit `wine`, and Arch/CachyOS ships
# wine 11.x with no wine64 binary at all -- so a bare `command -v wine64` made
# this lane SKIP on every modern Wine while printing a reason that read like a
# missing install. Prefer wine64 when it exists (older split installs), else
# wine. Measured 2026-08-09 on this box: wine-11.14, no wine64.
WINE="$(command -v wine64 || command -v wine || true)"
[ -n "$WINE" ] || { echo "SKIP: neither wine64 nor wine on PATH"; exit 0; }
[ -n "$MINGWCC" ] || { echo "SKIP: x86_64-w64-mingw32-gcc not on PATH"; exit 0; }
export LD_PRELOAD=
FILTER="${WINE_FFI_FILTER:-}"
T="$(mktemp -d)"; trap 'rm -rf "$T"; pkill -f "wine.*/tmp/tmp\." 2>/dev/null; pkill wineserver 2>/dev/null' EXIT
fail=0; pass=0

mkdir -p build
make -s build/tycho_rt_embed.h
if [ ! -x build/tychoc-mingw.exe ]; then
    "$MINGWCC" -O2 -fwrapv -std=c11 -Ibuild src/tychoc.c -o build/tychoc-mingw.exe \
        || { echo "FAIL: mingw compiler build"; exit 2; }
fi
CORELIB="Z:\\$(pwd | sed 's|^/||; s|/|\\|g')\\corelib"
E="env -u LD_PRELOAD WINEDEBUG=-all TYCHO_CORELIB=$CORELIB $WINE ./build/tychoc-mingw.exe"
W="env -u LD_PRELOAD WINEDEBUG=-all WINEPATH=Z:\\\\usr\\\\x86_64-w64-mingw32\\\\lib $WINE"

# <label> <entry.ty> <extra-shims...> <extra-libs...> -- run under wine, diff vs <expected>
run_exe() {
    label="$1"; entry="$2"; shims="$3"; libs="$4"; expect="$5"
    case "$label" in *"$FILTER"*) ;; *) return ;; esac
    $E "$entry" --emit-c -o "$T/f" >/dev/null 2>&1 || { echo "FAIL $label (emit)"; fail=$((fail+1)); return; }
    S=$($E "$entry" --print-shims 2>/dev/null | sed 's|^Z:||; s|\\|/|g' | tr '\n' ' ')
    "$MINGWCC" -O3 -fwrapv -pthread -o "$T/f.exe" "$T/f.c" $S $shims $libs -lm 2>"$T/f.cc" \
        || { echo "FAIL $label (cc)"; head -2 "$T/f.cc" | sed 's/^/      /'; fail=$((fail+1)); return; }
    out=$($W "$T/f.exe" 2>/dev/null)
    if [ "$out" = "$expect" ]; then echo "ok    $label (under Wine)"; pass=$((pass+1))
    else echo "FAIL $label (got '$out' want '$expect')"; fail=$((fail+1)); fi
}

# <label> <program> -- the MINGW compiler must REJECT it
reject() {
    label="$1"; prog="$2"
    case "$label" in *"$FILTER"*) ;; *) return ;; esac
    printf '%b' "$prog" > "$T/rej.ty"
    # -o, not --emit-c: the shell-injection guard lives at LINK-line construction
    # (src/tychoc.c cc_safe_name), which --emit-c returns before. The rejected
    # programs are all invalid, so the refusal fires before any cc invocation. */
    if $E "$T/rej.ty" -o "$T/rej" >/dev/null 2>&1; then
        echo "FAIL $label (mingw compiler ACCEPTED it)"; fail=$((fail+1))
    else echo "ok    $label (mingw compiler rejects, under Wine)"; pass=$((pass+1)); fi
}

# the fixture C library, built for Windows
"$MINGWCC" -O2 -fwrapv -c tests/ffi/demo.c -o "$T/demo.o" || { echo "FAIL: demo.c"; exit 2; }
"$MINGWAR" rcs "$T/libffidemo.a" "$T/demo.o"

echo ">>> ffi golden + shim + package legs (vs the lane's expected values)"
# the fixture programs the lane generates itself (tests/ffi/run.sh:41-44, :94-96)
printf 'extern fn ffi_triple(x: int) -> int\nfn main():\n    print(f"triple={ffi_triple(14)}\\n")\n' > "$T/shimtest.ty"
printf 'extern fn ffi_add32(a: u32, b: u32) -> u32\nextern fn ffi_shl64(x: u32, n: i32) -> u64\nextern fn ffi_negbyte(x: u8) -> i8\nfn main():\n    print(f"{ffi_add32(4000000000, 300000000)} {ffi_shl64(1, 33)} {ffi_negbyte(5)}\\n")\n' > "$T/sz.ty"
case "golden" in *"$FILTER"*)
    $E tests/ffi/main.ty --emit-c -o "$T/main" >/dev/null 2>&1
    S=$($E tests/ffi/main.ty --print-shims 2>/dev/null | sed 's|^Z:||; s|\\|/|g' | tr '\n' ' ')
    "$MINGWCC" -O3 -fwrapv -pthread -o "$T/main.exe" "$T/main.c" $S -L"$T" -lffidemo -lm 2>/dev/null \
        || { echo "FAIL golden (cc)"; fail=$((fail+1)); }
    $W "$T/main.exe" 2>/dev/null > "$T/main.out"
    if cmp -s "$T/main.out" tests/ffi/expected.out; then echo "ok    golden (main.ty over mingw libffidemo, byte-identical)"; pass=$((pass+1))
    else echo "FAIL golden"; diff tests/ffi/expected.out "$T/main.out" | head -3; fail=$((fail+1)); fi ;; esac
run_exe "shim"   "$T/shimtest.ty"  "tests/ffi/shim.c" "" "triple=42"
run_exe "pkgext" "tests/ffi/pkgext/main.ty" "tests/ffi/shim.c" "" "tri6=42"
run_exe "sized"  "$T/sz.ty"        "tests/ffi/shim.c" "" "5032704 8589934592 -5"

echo ">>> compiler-side rejections through the mingw compiler"
hh='handle R:\n    free: hc\nextern fn ho(i: int) -> R\nextern fn hc(h: R) -> int\nextern fn hu(h: R) -> int\n'
reject "handle-reassign" "$hh fn main():\n    d := ho(1)\n    d = ho(2)\n"
reject "handle-container" "$hh fn main():\n    a := [ho(1)]\n    print(\"x\")\n"
reject "handle-return" "$hh fn bad() -> R:\n    return ho(1)\nfn main():\n    return\n"
reject "handle-capture" "$hh fn main():\n    d := ho(1)\n    f := fn() -> int: hu(d)\n    print(str(f()))\n"
reject "shell-inject-extern" 'extern "m; touch /tmp/wine_ffi_INJECTED" fn z() -> int\nfn main():\n    print(str(z()))\n'
reject "inout-string" 'extern "x" fn f(s: inout string)\nfn main():\n    print("x")\n'

echo
echo "wine-ffi: passed $pass  failed $fail"
[ "$fail" -eq 0 ] || { echo "wine-ffi: FAIL ($fail)"; exit 1; }
echo "wine-ffi: all green (approximation only; the ASan leg is the CI's)"
