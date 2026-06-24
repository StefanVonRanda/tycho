#!/bin/sh
# FFI Stage 1 regression harness. Builds the fixture C lib (tests/ffi/demo.c),
# then compiles tests/ffi/main.ty BOTH ways — via the C reference compiler
# (tychoc, which links -lffidemo itself from `extern "ffidemo"`) and via the
# self-hosted compiler (tychoc0, which emits C that we link) — and asserts both
# produce the golden tests/ffi/expected.out. Also recompiles the emitted C under
# ASan/UBSan to prove the string-return arena-copy is memory-clean (no UAF/leak).
# Re-record the golden with RECORD=1 sh tests/ffi/run.sh.
set -u
cd "$(dirname "$0")/../.." || exit 2                  # repo root
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc — run 'make' first"; exit 2; }
CC="${CC:-cc}"
RECORD="${RECORD:-0}"
golden="tests/ffi/expected.out"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0

# fixture C library: libffidemo.a (static, so the binary needs no LD path at run time)
$CC -O2 -fwrapv -c tests/ffi/demo.c -o "$T/demo.o" || { echo "FAIL: compiling demo.c"; exit 1; }
ar rcs "$T/libffidemo.a" "$T/demo.o"

# the self-hosted compiler
"$TYCHOC" compiler/tychoc0.ty -o "$T/h0" >/dev/null 2>&1 || { echo "FAIL: could not build tychoc0"; exit 1; }

# (1) C reference compiler: it resolves `extern "ffidemo"` -> -lffidemo on its own
# cc line; the Stage-3 `-L` flag points the linker at our static lib (no LIBRARY_PATH).
if ! "$TYCHOC" tests/ffi/main.ty -o "$T/c_bin" -L "$T" >"$T/c.log" 2>&1; then
    echo "FAIL: tychoc compile"; sed 's/^/      /' "$T/c.log"; fail=1
else
    "$T/c_bin" > "$T/c.out" 2>&1
fi

# (2) self-hosted compiler: emits C to stdout; we compile+link it ourselves.
if ! { "$T/h0" tests/ffi/main.ty > "$T/h0.c" 2>/dev/null && \
       LIBRARY_PATH="$T" $CC -O2 -fwrapv -std=c11 -o "$T/h0_bin" "$T/h0.c" -lffidemo -lm 2>"$T/h0.log"; }; then
    echo "FAIL: tychoc0 compile"; sed 's/^/      /' "$T/h0.log"; fail=1
else
    "$T/h0_bin" > "$T/h0.out" 2>&1
fi

# (3) ASan/UBSan over the emitted C: the str-return arena-copy must be clean.
if ! LIBRARY_PATH="$T" $CC -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1 -fwrapv \
        -std=c11 -o "$T/h0_san" "$T/h0.c" -lffidemo -lm 2>"$T/san.log"; then
    echo "FAIL: sanitizer cc"; sed 's/^/      /' "$T/san.log"; fail=1
else
    "$T/h0_san" > "$T/san.out" 2>"$T/san.err"; src=$?
    [ "$src" -eq 0 ] || { echo "FAIL: sanitizer exit $src"; sed 's/^/      /' "$T/san.err"; fail=1; }
    grep -qiE 'runtime error|AddressSanitizer|Sanitizer|ERROR: ' "$T/san.err" && { echo "FAIL: sanitizer report"; sed 's/^/      /' "$T/san.err"; fail=1; }
fi

# the two compilers must agree
if [ "$fail" -eq 0 ] && ! cmp -s "$T/c.out" "$T/h0.out"; then
    echo "FAIL: tychoc vs tychoc0 output differ"; diff "$T/c.out" "$T/h0.out" | sed 's/^/      /'; fail=1
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

# (5) Package-scoped extern: an extern declared+called inside a package. Both
# compilers must keep the C symbol unmangled (tychoc0 regression). Expect tri6=42.
if ! "$TYCHOC" tests/ffi/pkgext/main.ty -o "$T/pkg_c" --shim tests/ffi/shim.c >"$T/pkg.log" 2>&1; then
    echo "FAIL: pkg-extern tychoc compile"; sed 's/^/      /' "$T/pkg.log"; fail=1
else
    [ "$("$T/pkg_c" 2>&1)" = "tri6=42" ] || { echo "FAIL: pkg-extern tychoc output"; fail=1; }
fi
if ! { "$TYCHOC" tests/ffi/pkgext/main.ty --bundle 2>/dev/null | "$T/h0" > "$T/pkg_h0.c" 2>/dev/null && \
       $CC -O2 -fwrapv -std=c11 -o "$T/pkg_h0" "$T/pkg_h0.c" tests/ffi/shim.c -lm 2>"$T/pkg_h0.log"; }; then
    echo "FAIL: pkg-extern tychoc0 compile"; sed 's/^/      /' "$T/pkg_h0.log"; fail=1
else
    [ "$("$T/pkg_h0" 2>&1)" = "tri6=42" ] || { echo "FAIL: pkg-extern tychoc0 output"; fail=1; }
fi

# (6) Affine handle bans (FFI R2): BOTH compilers must REJECT each misuse — a
# handle returned, reassigned, stored in a container, or captured would double-free
# or dangle. Rejection is at compile time, so the opener/closer need not link.
hh='handle R:\n    free: hc\nextern fn ho(i: int) -> R\nextern fn hc(h: R) -> int\nextern fn hu(h: R) -> int\n'
reject_handle() {   # $1 = printf-escaped program body, $2 = label
    printf '%b' "$hh$1" > "$T/rej.ty"
    if "$TYCHOC" "$T/rej.ty" --emit-c -o "$T/rej" >/dev/null 2>&1; then echo "FAIL: handle-ban ($2): tychoc accepted it"; fail=1; fi
    if "$T/h0" < "$T/rej.ty" >/dev/null 2>&1; then echo "FAIL: handle-ban ($2): tychoc0 accepted it"; fail=1; fi
}
reject_handle 'fn main():\n    d := ho(1)\n    d = ho(2)\n' reassign
reject_handle 'fn main():\n    a := [ho(1)]\n    print("x")\n' container
reject_handle 'fn bad() -> R:\n    return ho(1)\nfn main():\n    return\n' return
reject_handle 'fn main():\n    d := ho(1)\n    f := fn() -> int: hu(d)\n    print(str(f()))\n' capture

if [ "$RECORD" = 1 ]; then cp "$T/c.out" "$golden"; echo "rec  ffi"; fi
if [ "$fail" -eq 0 ] && [ ! -f "$golden" ]; then echo "FAIL: no golden — run RECORD=1"; fail=1; fi
if [ "$fail" -eq 0 ] && ! cmp -s "$T/c.out" "$golden"; then
    echo "FAIL: output != golden"; diff "$golden" "$T/c.out" | sed 's/^/      /'; fail=1
fi

[ "$fail" -eq 0 ] && echo "ffi: green (tychoc + tychoc0 agree, ASan-clean, match golden — scalars+string, ptr handles, null/is_null, -L + --shim, package-scoped extern)" || { echo "ffi: FAIL"; exit 1; }
