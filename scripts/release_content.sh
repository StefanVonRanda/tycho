# What is INSIDE the release archive -- not whether the script that built it
# exited 0. `make release-check` rebuilds both archives and asserts only that two
# builds are byte-identical (`Makefile@release-check`), which is green for an
# archive containing the wrong compiler, four .exe files that cannot start, and
# no runtime/ directory. All three shipped; commit 7534812f found them by hand.
#
# Usage:
#   sh scripts/release_content.sh              build both archives, then check them
#   sh scripts/release_content.sh --selfcheck  the three historical defects, each
#                                              rebuilt for real and each required
#                                              to redden the leg that names it
#
# If a wine lane was killed mid-run, `wineserver -k` first.
set -u
cd "$(dirname "$0")/.." || exit 2
root="$(pwd)"
export LD_PRELOAD=

MINGWCC="$(command -v x86_64-w64-mingw32-gcc || true)"
OBJDUMP="$(command -v x86_64-w64-mingw32-objdump || command -v objdump || true)"
WINE="$(command -v wine64 || command -v wine || true)"
W="env -u LD_PRELOAD WINEDEBUG=-all $WINE"

fail=0
legs=0
ok()   { legs=$((legs + 1)); echo "   ok  $*"; }
bad()  { legs=$((legs + 1)); fail=$((fail + 1)); echo "FAIL $*"; }

T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT HUP INT TERM
mkdir -p "$T/src"
printf 'package main\nimport "core:strings"\nfn main():\n    println(strings.to_upper("release ok"))\n' > "$T/src/t.ty"

# Windows system DLLs, which the archive is not expected to carry. Anything else
# an .exe imports must be a file in the archive, or the .exe cannot start:
# libwinpthread-1.dll was imported by all four and carried by none.
sysdll=" kernel32.dll msvcrt.dll advapi32.dll user32.dll ws2_32.dll shell32.dll bcrypt.dll ucrtbase.dll "

# ---------------------------------------------------------------- shared legs

# Every .exe's imports, against the allowlist plus whatever the archive carries.
check_imports() {
    st="$1"; tag="$2"
    carried=""
    for f in "$st"/*.dll; do [ -e "$f" ] && carried="$carried $(basename "$f" | tr 'A-Z' 'a-z')"; done
    for exe in "$st"/*.exe; do
        [ -e "$exe" ] || { bad "$tag: no .exe in the archive"; return; }
        miss=""
        for d in $("$OBJDUMP" -p "$exe" | sed -n 's/.*DLL Name: //p' | sort -u); do
            l="$(echo "$d" | tr 'A-Z' 'a-z')"
            case "$sysdll$carried " in *" $l "*) ;; *) miss="$miss $d" ;; esac
        done
        if [ -n "$miss" ]; then
            bad "$tag: $(basename "$exe") imports a DLL the archive does not carry:$miss"
        else
            ok "$tag: $(basename "$exe") imports only DLLs the archive can rely on"
        fi
    done
}

# Layout + the runtime's identity. tychoc1 COPIES runtime/tycho_rt.c into its
# output at emit time rather than embedding it, so a missing or drifted file is
# a compiler that dies on the first program a user compiles.
check_layout() {
    st="$1"; tag="$2"; shift 2
    for want in "$@"; do
        [ -e "$st/$want" ] && ok "$tag: $want present" || bad "$tag: $want MISSING from the archive"
    done
    if [ -f "$st/runtime/tycho_rt.c" ]; then
        cmp -s "$st/runtime/tycho_rt.c" "$root/runtime/tycho_rt.c" \
            && ok "$tag: runtime/tycho_rt.c identical to the repo's" \
            || bad "$tag: runtime/tycho_rt.c DIFFERS from runtime/tycho_rt.c"
    fi
}

# ------------------------------------------------------------- native archive

check_native() {
    st="$1"; ver="$2"
    check_layout "$st" native tychoc tychofmt tycho-lsp tycho-debug corelib runtime/tycho_rt.c README.md LICENSE

    v="$("$st/tychoc" --version 2>/dev/null | awk '{print $2}')"
    [ "$v" = "$ver" ] && ok "native: the packaged tychoc reports $ver" \
                      || bad "native: the packaged tychoc reports '$v', expected '$ver'"

    # Which compiler is in the box. The two disagree on this program, so the
    # comparison is not decoration -- assert the disagreement before using it.
    ( cd "$st" && ./tychoc "$root/bench/treewalk.ty" --emit-c -o "$T/nat_pkg" ) >/dev/null 2>&1
    ./tychoc1 "$root/bench/treewalk.ty" --emit-c -o "$T/nat_c1" >/dev/null 2>&1
    ./tychoc  "$root/bench/treewalk.ty" --emit-c -o "$T/nat_c0" >/dev/null 2>&1
    if cmp -s "$T/nat_c1.c" "$T/nat_c0.c"; then
        bad "native: tychoc1 and tychoc emit the SAME C for bench/treewalk.ty -- the identity leg below proves nothing"
    else
        ok "native: tychoc1 and tychoc emit different C, so the identity leg discriminates"
        cmp -s "$T/nat_pkg.c" "$T/nat_c1.c" \
            && ok "native: the packaged tychoc IS tychoc1 (emit byte-identical)" \
            || bad "native: the packaged tychoc is NOT tychoc1 (emit differs from ./tychoc1's)"
    fi

    # corelib found beside the binary, from a cwd that is not the repo.
    ( cd "$st" && env -u TYCHO_CORELIB ./tychoc "$T/src/t.ty" -o "$T/nat_t" ) >/dev/null 2>&1
    out="$("$T/nat_t" 2>/dev/null || true)"
    [ "$out" = "RELEASE OK" ] && ok "native: compiled and RAN a core:strings program with no TYCHO_CORELIB" \
                              || bad "native: the packaged compiler did not build+run a core:strings program (got '$out')"
}

# ------------------------------------------------------------- mingw archive

check_mingw() {
    st="$1"; ver="$2"
    check_layout "$st" mingw tychoc.exe tychofmt.exe tycho-lsp.exe tycho-debug.exe corelib runtime/tycho_rt.c README.md LICENSE
    check_imports "$st" mingw

    # An import the loader cannot satisfy is exit 53 under wine, and that is what
    # every .exe this loop shipped before 2026-09-05 did. --version is not a flag
    # tycho-debug takes, so the assertion is that the process STARTED, not that
    # it succeeded.
    for exe in "$st"/*.exe; do
        $W "$exe" --version >/dev/null 2>&1; rc=$?
        [ "$rc" -eq 53 ] && bad "mingw: $(basename "$exe") does not start under wine (exit 53 -- a DLL is missing)" \
                         || ok "mingw: $(basename "$exe") starts under wine (exit $rc)"
    done

    v="$($W "$st/tychoc.exe" --version 2>/dev/null | awk '{print $2}')"
    [ "$v" = "$ver" ] && ok "mingw: the packaged tychoc.exe reports $ver" \
                      || bad "mingw: the packaged tychoc.exe reports '$v', expected '$ver'"

    # Same identity question as the native leg. The emitted C carries no path, so
    # the two runs are comparable despite different cwds -- and the disagreement
    # between tychoc1 and tychoc is asserted in check_native before this is used.
    rm -f "$T/win_pkg.c"
    ( cd "$st" && $W ./tychoc.exe "$root/bench/treewalk.ty" --emit-c -o "$T/win_pkg" ) >/dev/null 2>&1
    ./tychoc1 "$root/bench/treewalk.ty" --emit-c -o "$T/win_c1" >/dev/null 2>&1
    if [ ! -s "$T/win_pkg.c" ]; then
        bad "mingw: the packaged tychoc.exe emitted nothing for bench/treewalk.ty (a missing runtime/ dies here)"
    else
        cmp -s "$T/win_pkg.c" "$T/win_c1.c" \
            && ok "mingw: the packaged tychoc.exe IS tychoc1 (emit byte-identical to the native tychoc1's)" \
            || bad "mingw: the packaged tychoc.exe is NOT tychoc1 (emit differs from ./tychoc1's)"
    fi

    # The whole route: emit under wine with corelib beside the binary, link with
    # the shims the packaged compiler itself named, and RUN the result.
    rm -f "$T/win_t.c" "$T/win_t.exe"
    ( cd "$st" && env -u TYCHO_CORELIB $W ./tychoc.exe "$T/src/t.ty" --emit-c -o "$T/win_t" ) >/dev/null 2>&1
    shims="$( cd "$st" && env -u TYCHO_CORELIB $W ./tychoc.exe "$T/src/t.ty" --print-shims 2>/dev/null | tr -d '\r' | tr '\n' ' ' )"
    out=""
    if [ -s "$T/win_t.c" ] && [ -n "$shims" ]; then
        # shellcheck disable=SC2086
        ( cd "$st" && "$MINGWCC" -O1 -fwrapv -static -pthread -o "$T/win_t.exe" "$T/win_t.c" $shims -lm ) 2>/dev/null
        out="$($W "$T/win_t.exe" 2>/dev/null | tr -d '\r' || true)"
    fi
    [ "$out" = "RELEASE OK" ] && ok "mingw: emitted, linked and RAN a core:strings program under wine with no TYCHO_CORELIB" \
                              || bad "mingw: the packaged compiler did not build+run a core:strings program under wine (got '$out')"
}

# ------------------------------------------------------------------ selfcheck
#
# Each control rebuilds the real defect rather than simulating it, asserts the
# substitution landed, and requires the named leg to redden.

ctl_pass=0; ctl_fail=0
ctl() {
    what="$1"; want="$2"; log="$3"
    if grep -q "$want" "$log"; then
        ctl_pass=$((ctl_pass + 1)); echo "   ok  control: $what reddens the lane -- $(grep -m1 "$want" "$log")"
    else
        ctl_fail=$((ctl_fail + 1)); echo "FAIL control: $what did NOT redden the lane (expected a leg matching /$want/)"
    fi
}

selfcheck() {
    ver="$1"; base="$2"
    echo ">> selfcheck: the three defects commit 7534812f found by hand"

    # [C1] the archive ships the BOOTSTRAP compiler, which is what it did until
    # 7534812f. Built from src/tychoc.c, exactly as the old release.sh did.
    c="$T/c1"; rm -rf "$c"; cp -r "$base" "$c"
    make -s build/tycho_rt_embed.h >/dev/null 2>&1
    "$MINGWCC" -O2 -fwrapv -std=c11 -Ibuild src/tychoc.c -o "$c/tychoc.exe" 2>/dev/null \
        || { echo "FAIL control: could not cross-build src/tychoc.c"; ctl_fail=$((ctl_fail+1)); }
    if $W "$c/tychoc.exe" --version 2>/dev/null | grep -q "^tychoc "; then
        echo "   substitution applied: $c/tychoc.exe is src/tychoc.c ($($W "$c/tychoc.exe" --version 2>/dev/null | tr -d '\r'))"
        ( fail=0; legs=0; check_mingw "$c" "$ver" ) > "$T/c1.log" 2>&1
        ctl "the bootstrap compiler in place of tychoc1" "is NOT tychoc1" "$T/c1.log"
    fi

    # [C2] -pthread with no -static. The link line is release.sh's own, minus the
    # one flag; the .exe then imports libwinpthread-1.dll, which nothing carries.
    c="$T/c2"; rm -rf "$c"; cp -r "$base" "$c"
    ./tychoc tools/tychofmt.ty --emit-c -o "$T/fmt" >/dev/null 2>&1
    "$MINGWCC" -O2 -fwrapv -pthread -o "$c/tychofmt.exe" "$T/fmt.c" -lm 2>/dev/null
    if "$OBJDUMP" -p "$c/tychofmt.exe" 2>/dev/null | grep -qi winpthread; then
        echo "   substitution applied: $c/tychofmt.exe imports $("$OBJDUMP" -p "$c/tychofmt.exe" | sed -n 's/.*DLL Name: //p' | grep -i winpthread)"
        ( fail=0; legs=0; check_mingw "$c" "$ver" ) > "$T/c2.log" 2>&1
        ctl "-static dropped from the link line" "imports a DLL the archive does not carry" "$T/c2.log"
        ctl "-static dropped from the link line (the .exe cannot start)" "exit 53" "$T/c2.log"
    else
        echo "FAIL control: the -static-less rebuild did not import winpthread; the control is dead"
        ctl_fail=$((ctl_fail + 1))
    fi

    # [C3] no runtime/ directory, which is what the mingw leg staged until
    # 7534812f. The emit legs die: tychoc1 copies the runtime in at emit time.
    c="$T/c3"; rm -rf "$c"; cp -r "$base" "$c"; rm -rf "$c/runtime"
    if [ ! -e "$c/runtime" ]; then
        echo "   substitution applied: $c/runtime removed"
        ( fail=0; legs=0; check_mingw "$c" "$ver" ) > "$T/c3.log" 2>&1
        ctl "runtime/ removed from the archive" "runtime/tycho_rt.c MISSING" "$T/c3.log"
        ctl "runtime/ removed from the archive (the compiler dies on the first program)" "emitted nothing" "$T/c3.log"
    fi

    # And the revert: the untouched archive must still be clean, or every control
    # above is measuring a lane that reddens for everything.
    ( fail=0; legs=0; check_mingw "$base" "$ver" ) > "$T/c0.log" 2>&1
    if grep -q '^FAIL' "$T/c0.log"; then
        echo "FAIL control: the UNMUTATED archive reddens -- the three controls above prove nothing"
        sed -n 's/^FAIL/     FAIL/p' "$T/c0.log"
        ctl_fail=$((ctl_fail + 1))
    else
        ctl_pass=$((ctl_pass + 1)); echo "   ok  control: the unmutated archive stays clean"
    fi

    echo "release-content selfcheck: $ctl_pass ok, $ctl_fail failed"
    [ "$ctl_fail" -eq 0 ] || exit 1
    exit 0
}

# ----------------------------------------------------------------------- main

[ -n "$WINE" ]    || { echo "SKIP release-content: neither wine64 nor wine on PATH"; exit 0; }
[ -n "$MINGWCC" ] || { echo "SKIP release-content: x86_64-w64-mingw32-gcc not on PATH"; exit 0; }
[ -n "$OBJDUMP" ] || { echo "SKIP release-content: no objdump on PATH"; exit 0; }

make -s tychoc tychoc1 >/dev/null || { echo "release-content: build failed" >&2; exit 2; }
ver="$(./tychoc1 --version | awk '{print $2}')"
os="$(uname -s | tr '[:upper:]' '[:lower:]')"; arch="$(uname -m)"
nat="dist/tycho-v$ver-$os-$arch"; win="dist/tycho-v$ver-mingw64-$arch"

echo ">> building both archives (scripts/release.sh v$ver)"
sh scripts/release.sh "v$ver" >/dev/null || { echo "release-content: native release.sh failed" >&2; exit 1; }
sh scripts/release.sh "v$ver" --mingw >/dev/null || { echo "release-content: mingw release.sh failed" >&2; exit 1; }

mkdir -p "$T/x"
tar -C "$T/x" -xzf "$nat.tar.gz" || exit 2
tar -C "$T/x" -xzf "$win.tar.gz" || exit 2
nats="$T/x/$(basename "$nat")"; wins="$T/x/$(basename "$win")"

case "${1:---run}" in
    --selfcheck) selfcheck "$ver" "$wins" ;;
    --run) ;;
    *) echo "usage: scripts/release_content.sh [--selfcheck]" >&2; exit 2 ;;
esac

echo ">> $nat.tar.gz"
check_native "$nats" "$ver"
echo ">> $win.tar.gz"
check_mingw "$wins" "$ver"

echo "release-content: $legs legs, $fail failed"
[ "$fail" -eq 0 ] || exit 1
