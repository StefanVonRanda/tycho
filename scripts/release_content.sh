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
. ./scripts/shlib.sh          # `timeout` is not in the macOS base system
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

# --print-shims from the packaged compiler answers with the paths the COMPILER
# resolved. Since the argv0 fix those are absolute WINDOWS paths (`Z:\dir\...`)
# whenever wine hands the exe its full path, which a real Windows cc takes and
# this Linux cross-gcc cannot; Z: is wine's root, so dropping the drive and
# flipping the separators gives the same file back as a POSIX path.
winshims() {
    ( cd "$1" && env -u TYCHO_CORELIB $W "$2" "$3" --print-shims 2>/dev/null ) \
        | tr -d '\r' | tr '\\\\' '/' | sed 's/^[A-Za-z]://' | tr '\n' ' '
}

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

# ---------------------------------------------------- native portability legs
#
# The mingw leg has STARTED every .exe under wine since 2026-09-05; nothing ever
# started the native binaries, and that asymmetry is why a glibc floor shipped
# unseen. This host's `cc` defaults to __STDC_VERSION__ 202311L, glibc redirects
# strtol to __isoc23_strtol@GLIBC_2.38, and tychofmt/tycho-lsp/tycho-debug then
# refuse to start on Debian 12 (2.36), Ubuntu 22.04 (2.35) and Rocky 9 (2.34).
# The gate's assertion is the SYMBOL TABLE, not a container: `objdump -T` and the
# maximum GLIBC_ version in it, which is exactly what the dynamic loader compares.
# 2.17 is the ceiling because it predates every distro still receiving updates;
# a -static-pie binary has no versioned GLIBC symbol at all and reads as "none".
maxglibc="2.17"

# Every regular executable directly in the archive root. corelib/ carries .ty and
# .c sources only, so the root is the whole shipped executable set.
archive_exes() {
    for f in "$1"/*; do
        [ -f "$f" ] && [ -x "$f" ] && printf '%s\n' "$f"
    done
}

glibc_floor() {
    v="$("$OBJDUMP" -T "$1" 2>/dev/null | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' | sort -V | tail -1)"
    [ -n "$v" ] && printf '%s' "$v" || printf 'none'
}

check_native_start() {
    st="$1"; tag="$2"
    n=0
    for exe in $(archive_exes "$st"); do
        n=$((n + 1))
        err="$(timeout 20 "$exe" --version </dev/null 2>&1 >/dev/null)"; rc=$?
        # A loader failure is the whole subject: rc 127 plus ld.so's own words.
        case "$err" in
            *"not found"*GLIBC*|*GLIBC*"not found"*|*"error while loading shared libraries"*)
                bad "$tag: $(basename "$exe") does not START on this host -- $(printf '%s' "$err" | head -1)" ;;
            *)
                [ "$rc" -eq 127 ] \
                    && bad "$tag: $(basename "$exe") exited 127 (not executed)" \
                    || ok "$tag: $(basename "$exe") starts (exit $rc)" ;;
        esac
    done
    [ "$n" -ge 4 ] && ok "$tag: $n executables in the archive were started" \
                   || bad "$tag: only $n executables found in the archive -- the start legs cover almost nothing"
}

check_native_glibc() {
    st="$1"; tag="$2"
    for exe in $(archive_exes "$st"); do
        f="$(glibc_floor "$exe")"
        if [ "$f" = "none" ]; then
            ok "$tag: $(basename "$exe") needs no versioned glibc symbol (statically linked)"
        elif [ "$(printf '%s\n%s\n' "$f" "$maxglibc" | sort -V | tail -1)" = "$maxglibc" ]; then
            ok "$tag: $(basename "$exe") glibc floor $f (<= $maxglibc)"
        else
            bad "$tag: $(basename "$exe") requires GLIBC_$f, above the $maxglibc ceiling -- it cannot start on an older distro"
        fi
    done
}

# ------------------------------------------------------------- native archive

check_native() {
    st="$1"; ver="$2"
    check_layout "$st" native tychoc tychofmt tycho-lsp tycho-debug corelib runtime/tycho_rt.c README.md LICENSE
    check_native_start "$st" native
    check_native_glibc "$st" native

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

    # A backslash is a legal byte in a POSIX filename, so `dir_of` may not cut on
    # it here -- `back\slash.ty` importing `./p` resolves only while the whole
    # name is ONE component. This is the half the mingw leg below cannot see, and
    # the reason the Windows fix is guarded rather than unconditional.
    rm -rf "$T/bs"; mkdir -p "$T/bs/p"
    printf 'package p\nfn hi() -> string:\n    return "POSIX OK"\n' > "$T/bs/p/p.ty"
    printf 'package main\nimport "./p"\nfn main():\n    println(p.hi())\n' > "$T/bs/back\\slash.ty"
    ( cd "$st" && env -u TYCHO_CORELIB ./tychoc "$T/bs/back\\slash.ty" -o "$T/bs/out" ) >/dev/null 2>&1
    out="$("$T/bs/out" 2>/dev/null || true)"
    [ "$out" = "POSIX OK" ] && ok "native: a source path with a literal backslash did compile (one filename, not two components)" \
                            || bad "native: a source path with a literal backslash did not compile (got '$out') -- dir_of is cutting on a POSIX filename byte"
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
    shims="$(winshims "$st" ./tychoc.exe "$T/src/t.ty")"
    out=""
    if [ -s "$T/win_t.c" ] && [ -n "$shims" ]; then
        # shellcheck disable=SC2086
        ( cd "$st" && "$MINGWCC" -O1 -fwrapv -static -pthread -o "$T/win_t.exe" "$T/win_t.c" $shims -lm ) 2>/dev/null
        out="$($W "$T/win_t.exe" 2>/dev/null | tr -d '\r' || true)"
    fi
    [ "$out" = "RELEASE OK" ] && ok "mingw: emitted, linked and RAN a core:strings program under wine with no TYCHO_CORELIB" \
                              || bad "mingw: the packaged compiler did not build+run a core:strings program under wine (got '$out')"

    # The same route from a FOREIGN cwd, naming the .exe by an absolute path --
    # tychoc.exe on PATH, which is the only shape a Windows user who installed
    # tycho actually types. Every leg above cd's into the archive first, which is
    # why `compiler/types/load.ty@dir_of` cutting on '/' alone was invisible: wine
    # hands the program `Z:\...\tychoc.exe` as argv[0], that returned ".", and the
    # corelib and runtime fallbacks beside the binary were both dead.
    rm -f "$T/far_t.c" "$T/far_t.exe"
    ( cd "$T/src" && env -u TYCHO_CORELIB $W "$st/tychoc.exe" t.ty --emit-c -o "$T/far_t" ) >/dev/null 2>&1
    out=""
    if [ ! -s "$T/far_t.c" ]; then
        out="(nothing emitted)"
    elif ! grep -q arena_alloc_slow "$T/far_t.c"; then
        # A separate assertion on purpose: corelib_root and write_runtime are two
        # independent argv0-relative fallbacks and either can die on its own.
        out="(emitted, but the runtime was not copied in)"
    else
        shims="$(winshims "$T/src" "$st/tychoc.exe" t.ty)"
        # shellcheck disable=SC2086
        [ -n "$shims" ] && "$MINGWCC" -O1 -fwrapv -static -pthread -o "$T/far_t.exe" "$T/far_t.c" $shims -lm 2>/dev/null
        out="$($W "$T/far_t.exe" 2>/dev/null | tr -d '\r' || true)"
    fi
    [ "$out" = "RELEASE OK" ] && ok "mingw: tychoc.exe named by absolute path from a foreign cwd built and RAN the same program" \
                              || bad "mingw: tychoc.exe from a foreign cwd could not build a core:strings program (got '$out') -- argv0-relative lookup is dead"

    # The SOURCE path spelled the way a Windows shell hands it over --
    # `tychoc.exe C:\proj\main.ty`. Every leg above names the source POSIX-style,
    # so none of them can see this: `compiler/types/load.ty@dir_of` cut on '/'
    # alone, answered ".", and the compile died before reading a byte.
    rm -f "$T/bsrc.c" "$T/bsrc.exe"
    wsrc="Z:$(printf '%s' "$T/src/t.ty" | tr '/' '\\')"
    ( cd "$st" && env -u TYCHO_CORELIB $W ./tychoc.exe "$wsrc" --emit-c -o "$T/bsrc" ) >/dev/null 2>&1
    out=""
    if [ ! -s "$T/bsrc.c" ]; then
        out="(nothing emitted)"
    else
        shims="$(winshims "$st" ./tychoc.exe "$wsrc")"
        # shellcheck disable=SC2086
        [ -n "$shims" ] && "$MINGWCC" -O1 -fwrapv -static -pthread -o "$T/bsrc.exe" "$T/bsrc.c" $shims -lm 2>/dev/null
        out="$($W "$T/bsrc.exe" 2>/dev/null | tr -d '\r' || true)"
    fi
    [ "$out" = "RELEASE OK" ] && ok "mingw: a BACKSLASH source path (Windows-spelled, drive and all) built and RAN the same program" \
                              || bad "mingw: a backslash source path could not build a core:strings program (got '$out') -- dir_of does not cut on '\\' on Windows"
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
    echo ">> selfcheck: the three defects commit 7534812f found by hand, plus the argv0 one"

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

    # [C4] dir_of cutting on '/' alone, which is what compiler/types/load.ty did
    # until 2026-09-05. Rebuilt for real from a mutated copy of the compiler, so
    # the leg is scored against the actual defect rather than a simulation.
    c="$T/c4"; rm -rf "$c"; cp -r "$base" "$c"
    rm -rf "$T/c4src"; mkdir -p "$T/c4src"; cp -r compiler corelib "$T/c4src/"
    sed 's/if p\[i\] == 47 or p\[i\] == 92:/if p[i] == 47:/' "$T/c4src/compiler/types/load.ty" > "$T/c4.patched" && mv "$T/c4.patched" "$T/c4src/compiler/types/load.ty"
    if grep -q 'p\[i\] == 47 or p\[i\] == 92' "$T/c4src/compiler/types/load.ty"; then
        echo "FAIL control: the exe_dir_of mutation did not apply"; ctl_fail=$((ctl_fail + 1))
    else
        echo "   substitution applied: exe_dir_of in $T/c4src cuts on 47 only ($(grep -c 'p\[i\] == 92' "$T/c4src/compiler/types/load.ty") backslash tests left)"
        ./tychoc "$T/c4src/compiler/main.ty" --emit-c -o "$T/c4c" >/dev/null 2>&1
        # shellcheck disable=SC2086
        "$MINGWCC" -O1 -fwrapv -static -pthread -o "$c/tychoc.exe" "$T/c4c.c" \
            $(./tychoc "$T/c4src/compiler/main.ty" --print-shims 2>/dev/null | tr '\n' ' ') -lm 2>/dev/null
        ( fail=0; legs=0; check_mingw "$c" "$ver" ) > "$T/c4.log" 2>&1
        ctl "dir_of cutting on '/' alone" "argv0-relative lookup is dead" "$T/c4.log"
        grep -q "^   ok  mingw: emitted, linked and RAN a core:strings program" "$T/c4.log" \
            && { ctl_pass=$((ctl_pass + 1)); echo "   ok  control: the mutated compiler still works with cwd = the archive, so the leg above is about the CWD"; } \
            || { ctl_fail=$((ctl_fail + 1)); echo "FAIL control: the mutated compiler fails from the archive cwd too -- C4 is not isolating the argv0 path"; }
    fi

    # [C5] dir_of ignoring '\' on Windows, which is what it did until 2026-09-06.
    # The mingw twin of C4: same function, the SOURCE path rather than argv0.
    c="$T/c5"; rm -rf "$c"; cp -r "$base" "$c"
    rm -rf "$T/c5src"; mkdir -p "$T/c5src"; cp -r compiler corelib "$T/c5src/"
    sed 's/win := os.is_windows()/win := false/' "$T/c5src/compiler/types/load.ty" > "$T/c5.patched" && mv "$T/c5.patched" "$T/c5src/compiler/types/load.ty"
    rm -f "$c/tychoc.exe"
    if grep -q 'win := os.is_windows()' "$T/c5src/compiler/types/load.ty"; then
        echo "FAIL control: the dir_of mutation did not apply"; ctl_fail=$((ctl_fail + 1))
    else
        echo "   substitution applied: dir_of in $T/c5src answers the host predicate false ($(grep -c 'win := false' "$T/c5src/compiler/types/load.ty") site)"
        ./tychoc "$T/c5src/compiler/main.ty" --emit-c -o "$T/c5c" >/dev/null 2>&1
        # shellcheck disable=SC2046
        "$MINGWCC" -O1 -fwrapv -static -pthread -o "$c/tychoc.exe" "$T/c5c.c" \
            $(./tychoc "$T/c5src/compiler/main.ty" --print-shims 2>/dev/null | tr '\n' ' ') -lm 2>/dev/null
        [ -s "$c/tychoc.exe" ] || { echo "FAIL control: the C5 mutant did not BUILD -- the control is dead"; ctl_fail=$((ctl_fail + 1)); }
        ( fail=0; legs=0; check_mingw "$c" "$ver" ) > "$T/c5.log" 2>&1
        ctl "dir_of ignoring a backslash SOURCE path" "backslash source path could not build" "$T/c5.log"
        grep -q "^   ok  mingw: tychoc.exe named by absolute path from a foreign cwd" "$T/c5.log" \
            && { ctl_pass=$((ctl_pass + 1)); echo "   ok  control: the same mutant still builds a POSIX-spelled source path, so C5 is isolating the SOURCE path"; } \
            || { ctl_fail=$((ctl_fail + 1)); echo "FAIL control: the C5 mutant fails the POSIX-spelled legs too -- C5 is not isolating the source path"; }
    fi

    # [C6] the fix OVERSHOOTING: dir_of cutting on '\' with no host guard. No
    # Windows leg can see this one -- what breaks is the POSIX filename.
    c="$T/c6"; rm -rf "$c"; cp -r "$nats" "$c"
    rm -rf "$T/c6src"; mkdir -p "$T/c6src"; cp -r compiler corelib "$T/c6src/"
    sed 's/win := os.is_windows()/win := true/' "$T/c6src/compiler/types/load.ty" > "$T/c6.patched" && mv "$T/c6.patched" "$T/c6src/compiler/types/load.ty"
    rm -f "$c/tychoc"
    if grep -q 'win := os.is_windows()' "$T/c6src/compiler/types/load.ty"; then
        echo "FAIL control: the unconditional-cut mutation did not apply"; ctl_fail=$((ctl_fail + 1))
    else
        echo "   substitution applied: dir_of in $T/c6src answers the host predicate true ($(grep -c 'win := true' "$T/c6src/compiler/types/load.ty") site), so it cuts on 92 everywhere"
        ./tychoc "$T/c6src/compiler/main.ty" -o "$c/tychoc" >/dev/null 2>&1
        [ -x "$c/tychoc" ] || { echo "FAIL control: the C6 mutant did not BUILD -- the control is dead"; ctl_fail=$((ctl_fail + 1)); }
        ( fail=0; legs=0; check_native "$c" "$ver" ) > "$T/c6.log" 2>&1
        ctl "dir_of cutting on a POSIX filename's backslash" "literal backslash did not compile" "$T/c6.log"
    fi

    # [C7] the glibc floor: one tool rebuilt WITHOUT Makefile:TOOL_CFLAGS, which is
    # exactly how tychofmt/tycho-lsp/tycho-debug were built until 2026-09-06. Not a
    # simulation -- the same tychoc1 on the same source, one flag removed.
    c="$T/c7"; rm -rf "$c"; cp -r "$nats" "$c"
    rm -f "$c/tychofmt"
    env -u TYCHO_CFLAGS ./tychoc1 tools/tychofmt.ty -o "$c/tychofmt" >/dev/null 2>&1
    f7="$(glibc_floor "$c/tychofmt")"
    if [ ! -x "$c/tychofmt" ]; then
        echo "FAIL control: the C7 rebuild did not produce a binary -- the control is dead"; ctl_fail=$((ctl_fail + 1))
    elif [ "$f7" = "none" ] || [ "$(printf '%s\n%s\n' "$f7" "$maxglibc" | sort -V | tail -1)" = "$maxglibc" ]; then
        echo "FAIL control: tychofmt rebuilt without -static-pie has floor '$f7', at or under the $maxglibc ceiling -- this host's cc does not reproduce the defect, so C7 proves nothing"
        ctl_fail=$((ctl_fail + 1))
    else
        echo "   substitution applied: $c/tychofmt rebuilt with no TOOL_CFLAGS requires GLIBC_$f7 ($("$OBJDUMP" -T "$c/tychofmt" | grep -o '__isoc23_[a-z]*' | sort -u | tr '\n' ' ')), against $(glibc_floor "$nats/tychofmt") in the archive"
        ( fail=0; legs=0; check_native "$c" "$ver" ) > "$T/c7.log" 2>&1
        ctl "one tool built without -static-pie" "tychofmt requires GLIBC_$f7, above the $maxglibc ceiling" "$T/c7.log"
        grep -q "^   ok  native: tychoc needs no versioned glibc symbol" "$T/c7.log" \
            && { ctl_pass=$((ctl_pass + 1)); echo "   ok  control: the other three binaries in the same archive stay green, so the leg names the ONE that regressed"; } \
            || { ctl_fail=$((ctl_fail + 1)); echo "FAIL control: C7 reddens binaries it did not touch -- the leg is not per-binary"; }
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

    ( fail=0; legs=0; check_native "$nats" "$ver" ) > "$T/c0n.log" 2>&1
    if grep -q '^FAIL' "$T/c0n.log"; then
        echo "FAIL control: the UNMUTATED native archive reddens -- C6 proves nothing"
        sed -n 's/^FAIL/     FAIL/p' "$T/c0n.log"
        ctl_fail=$((ctl_fail + 1))
    else
        ctl_pass=$((ctl_pass + 1)); echo "   ok  control: the unmutated native archive stays clean"
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
