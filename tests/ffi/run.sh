set -u
cd "$(dirname "$0")/../.." || exit 2                  # repo root
TYCHOC="${TYCHOC:-./tychoc1}"
[ -x "$TYCHOC" ] || { echo "no ./tychoc — run 'make' first"; exit 2; }
CC="${CC:-cc}"
RECORD="${RECORD:-0}"
golden="tests/ffi/expected.out"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0

# fixture C library: libffidemo.a (static, so the binary needs no LD path at run time)
$CC -O2 -fwrapv -c tests/ffi/demo.c -o "$T/demo.o" || { echo "FAIL: compiling demo.c"; exit 1; }
ar rcs "$T/libffidemo.a" "$T/demo.o"

# (1) C reference compiler: it resolves `extern "ffidemo"` -> -lffidemo on its own
# cc line; the Stage-3 `-L` flag points the linker at our static lib (no LIBRARY_PATH).
if ! "$TYCHOC" tests/ffi/main.ty -o "$T/c_bin" -L "$T" >"$T/c.log" 2>&1; then
    echo "FAIL: tychoc compile"; sed 's/^/      /' "$T/c.log"; fail=1
else
    "$T/c_bin" > "$T/c.out" 2>&1
fi

# (2) ASan/UBSan over the emitted C: the str-return arena-copy must be clean.
#     SKIPPED on Windows: mingw gcc ships no -lasan/-lubsan (docs/internals/windows-port.md
#     phase 2); the native legs above and below still run.
if [ "$(uname -s | grep -ciE 'MSYS|MINGW|CYGWIN')" = 0 ]; then
if ! "$TYCHOC" tests/ffi/main.ty --emit-c -o "$T/hc" >"$T/emit.log" 2>&1; then
    echo "FAIL: tychoc --emit-c"; sed 's/^/      /' "$T/emit.log"; fail=1
fi
if ! LIBRARY_PATH="$T" $CC -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1 -fwrapv \
        -std=c11 -o "$T/h0_san" "$T/hc.c" -lffidemo -lm 2>"$T/san.log"; then
    echo "FAIL: sanitizer cc"; sed 's/^/      /' "$T/san.log"; fail=1
else
    "$T/h0_san" > "$T/san.out" 2>"$T/san.err"; src=$?
    [ "$src" -eq 0 ] || { echo "FAIL: sanitizer exit $src"; sed 's/^/      /' "$T/san.err"; fail=1; }
    grep -qiE 'runtime error|AddressSanitizer|Sanitizer|ERROR: ' "$T/san.err" && { echo "FAIL: sanitizer report"; sed 's/^/      /' "$T/san.err"; fail=1; }
fi
else
    echo "SKIP: ASan/UBSan leg (no mingw -lasan/-lubsan -- docs/internals/windows-port.md phase 2)"
fi

# (4) Stage 3 --shim: an extern implemented by a companion C file, compiled and
# linked alongside the generated C with no prebuilt library.
printf 'extern fn ffi_triple(x: int) -> int\nfn main():\n    print(f"triple={ffi_triple(14)}\\n")\n' > "$T/shimtest.ty"
if ! "$TYCHOC" "$T/shimtest.ty" -o "$T/shimbin" --shim tests/ffi/shim.c >"$T/shim.log" 2>&1; then
    echo "FAIL: --shim compile"; sed 's/^/      /' "$T/shim.log"; fail=1
else
    shimout="$("$T/shimbin" 2>&1)"
    [ "$shimout" = "triple=42" ] || { echo "FAIL: --shim output '$shimout' != 'triple=42'"; fail=1; }
fi

if ! "$TYCHOC" tests/ffi/pkgext/main.ty -o "$T/pkg_c" --shim tests/ffi/shim.c >"$T/pkg.log" 2>&1; then
    echo "FAIL: pkg-extern tychoc compile"; sed 's/^/      /' "$T/pkg.log"; fail=1
else
    [ "$("$T/pkg_c" 2>&1)" = "tri6=42" ] || { echo "FAIL: pkg-extern tychoc output"; fail=1; }
fi

# (6) Affine handle bans (FFI R2): tychoc must REJECT each misuse — a
# handle returned, reassigned, stored in a container, or captured would double-free
# or dangle. Rejection is at compile time, so the opener/closer need not link.
hh='handle R:\n    free: hc\nextern fn ho(i: int) -> R\nextern fn hc(h: R) -> int\nextern fn hu(h: R) -> int\n'
reject_handle() {   # $1 = printf-escaped program body, $2 = label
    printf '%b' "$hh$1" > "$T/rej.ty"
    if "$TYCHOC" "$T/rej.ty" --emit-c -o "$T/rej" >/dev/null 2>&1; then echo "FAIL: handle-ban ($2): tychoc accepted it"; fail=1; fi
}
reject_handle 'fn main():\n    d := ho(1)\n    d = ho(2)\n' reassign
reject_handle 'fn main():\n    a := [ho(1)]\n    print("x")\n' container
reject_handle 'fn bad() -> R:\n    return ho(1)\nfn main():\n    return\n' return
reject_handle 'fn main():\n    d := ho(1)\n    f := fn() -> int: hu(d)\n    print(str(f()))\n' capture

# (7) Shell-injection guard: a library name from `extern "Lib"` (source) or --link
# (CLI) is spliced onto the cc / pkg-config shell line, so it is charset-checked --
# compiling an untrusted .ty must fail closed, never run a shell command. Assert
# rejection AND that the injected `touch <marker>` never fired.
mark="$T/INJECTED"; rm -f "$mark"
printf 'extern "m; touch %s" fn z() -> int\nfn main():\n    print(str(z()))\n' "$mark" > "$T/inj.ty"
if "$TYCHOC" "$T/inj.ty" -o "$T/inj" >/dev/null 2>&1; then echo "FAIL: injection extern compiled"; fail=1; fi
[ -f "$mark" ] && { echo "FAIL: extern-name injection executed a shell command"; fail=1; }
if "$TYCHOC" tests/ffi/main.ty --link "m; touch $mark" -L "$T" >/dev/null 2>&1; then echo "FAIL: injection --link compiled"; fail=1; fi
[ -f "$mark" ] && { echo "FAIL: --link injection executed a shell command"; fail=1; }

# (8) FFI R4 out-param fail-closed: only int/char/float/bool/ptr may be `inout`
# (a clean T* the C fn fills). `inout string` (a char** with no length header) and
# other non-trivial out-param shapes must be rejected.
# --emit-c, NEVER -o: `extern "x"` puts -lx on the link line and no such library
# exists, so a -o leg passed on `cannot find -lx` whatever the front end decided.
printf 'extern "x" fn f(s: inout string)\nfn main():\n    print("x")\n' > "$T/r4rej.ty"
# Positive control: the same shape with a permitted out-param type MUST compile,
# or the rejection above is not about `inout string`.
printf 'extern "x" fn f(s: inout int)\nfn main():\n    print("x")\n' > "$T/r4ok.ty"
if ! "$TYCHOC" "$T/r4ok.ty" --emit-c -o "$T/r4ok" >/dev/null 2>&1; then
    echo "FAIL: inout-int control rejected -- the R4 leg cannot distinguish string from int"; fail=1
fi
# Both front ends carry the ban now (src/tychoc.c:4724, compiler/types/tcheck.ty),
# so there is no exemption: an accept is a failure whichever compiler is under test.
if "$TYCHOC" "$T/r4rej.ty" --emit-c -o "$T/r4rej" >/dev/null 2>&1; then
    echo "FAIL: inout-string out-param accepted by $TYCHOC"; fail=1
fi

# (9) FFI-boundary sized ints (u8/u16/u32/u64/i8/i16/i32/i64): recognized ONLY in
# extern signatures; the value is `int` to Tycho but the emitted prototype uses the
# real C ABI type, so a u32 wraps at 2^32 and a u64 return carries >32 bits.
sz='extern fn ffi_add32(a: u32, b: u32) -> u32\nextern fn ffi_shl64(x: u32, n: i32) -> u64\nextern fn ffi_negbyte(x: u8) -> i8\nfn main():\n    print(f"{ffi_add32(4000000000, 300000000)} {ffi_shl64(1, 33)} {ffi_negbyte(5)}\\n")\n'
printf '%b' "$sz" > "$T/sz.ty"
szexp="5032704 8589934592 -5"
if ! "$TYCHOC" "$T/sz.ty" -o "$T/sz_c" --shim tests/ffi/shim.c >"$T/sz.log" 2>&1; then
    echo "FAIL: sized-ffi tychoc compile"; sed 's/^/      /' "$T/sz.log"; fail=1
else
    [ "$("$T/sz_c" 2>&1)" = "$szexp" ] || { echo "FAIL: sized-ffi tychoc output '$("$T/sz_c" 2>&1)' != '$szexp'"; fail=1; }
fi
printf 'fn main():\n    x: i16 = 3\n    if x > 0:\n        println("pos")\n' > "$T/szok.ty"
if ! "$TYCHOC" "$T/szok.ty" --emit-c -o "$T/szok" >/dev/null 2>&1; then echo "FAIL: first-class i16 rejected by tychoc"; fail=1; fi

if [ "$RECORD" = 1 ]; then cp "$T/c.out" "$golden"; echo "rec  ffi"; fi
if [ "$fail" -eq 0 ] && [ ! -f "$golden" ]; then echo "FAIL: no golden — run RECORD=1"; fail=1; fi
if [ "$fail" -eq 0 ] && ! cmp -s "$T/c.out" "$golden"; then
    echo "FAIL: output != golden"; diff "$golden" "$T/c.out" | sed 's/^/      /'; fail=1
fi

[ "$fail" -eq 0 ] && echo "ffi: green (tychoc: ASan-clean, matches golden — scalars+string, sized ints, ptr handles, null/is_null, -L + --shim, package-scoped extern)" || { echo "ffi: FAIL"; exit 1; }
