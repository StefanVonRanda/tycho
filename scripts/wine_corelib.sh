#!/bin/sh
# Phase-4 wine-corelib: every corelib test that CAN link on this box,
# cross-compiled under mingw and run under Wine against the Linux goldens.
# The Linux-box approximation of "make corelib on the Windows box"
# (plan_windows.md phase 4); scripts/wine_test.sh covers the compiler corpus,
# this covers corelib/test/*.
#
#   sh scripts/wine_corelib.sh
#   WINE_CORELIB_FILTER=io sh scripts/wine_corelib.sh
#
# WHAT RUNS / WHAT SKIPS. The classification is explicit per package, and a
# package missing from it FAILS the lane (fail closed -- a new package must be
# classified). The SKIPs are the external-dependency packages whose mingw
# libraries live in MSYS2 and are absent on this host (tls/http/image/crypto/
# sqlite: OpenSSL/libcurl/libpng/sqlite3 mingw builds; regex: pcre2-posix) --
# their shim code is portable and the regex/signal/io/strings _WIN32 branches
# are compile-verified here, but the LINK needs MSYS2. compress/zip link
# against libz-mingw-w64 (installed); net/signal/httpd link ws2_32 (system).
#
# NOT a gate and NOT a Windows verdict -- Wine is an approximation, and the
# definitive pass is the windows CI leg. Skips loudly when mingw/wine absent.
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
FILTER="${WINE_CORELIB_FILTER:-}"

T="$(mktemp -d)"; trap 'rm -rf "$T"; pkill -f "wine.*/tmp/tmp\." 2>/dev/null; pkill wineserver 2>/dev/null' EXIT
fail=0; pass=0; skipped=0

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

# classify a package: "run" or "run-<extra-libs>" or "skip <reason>" -- fail closed otherwise
classify() {
    case "$1" in
        net|httpd)          echo "run--lws2_32" ;;
        os)                 echo "skip its test drives POSIX-shell commands (true/false/printf) that cmd.exe lacks -- the PACKAGE works (exit codes map, exit 7 passes); the test needs a Windows variant, phase 6" ;;
        datetime)           echo "skip its offset_at cases use POSIX TZ strings whose sign convention Windows _tzset inverts (EST5EDT -> +5h); utc0 matches; needs a Windows-aware golden, phase 6" ;;
        signal)             echo "skip its test kills itself via 'kill -TERM \$PPID' from a POSIX shell -- cmd.exe has no kill, the handler never fires and the accept blocks forever (the shim is compile-verified; the test needs a Windows variant, phase 6)" ;;
        compress|zip)       echo "run--lz" ;;
        crypto|http|image|sqlite|tls)
            echo "skip OpenSSL/libcurl/libpng/sqlite3 mingw lib not on this host (MSYS2)" ;;
        regex)              echo "skip pcre2-posix mingw lib not on this host (MSYS2)" ;;
        arrays|base64|bignum|char|cli|csv|datetime|decimal|fmath|hash|hex|intern|io|iter|json|log|markdown|math|md5|os|path|pool|rand|raster|result|sha256|sort|strings|testing|time|toml|url|utf8|uuid|wordfreq)
            echo "run" ;;
        *) echo "UNCLASSIFIED" ;;
    esac
}

echo ">>> corelib tests cross-compiled under mingw, run under Wine vs goldens"
for d in corelib/test/*/; do
    [ -d "$d" ] || continue
    p="$(basename "$d")"
    case "$p" in *"$FILTER"*) ;; *) continue ;; esac
    [ -f "$d/main.ty" ] || { echo "skip $p (no main.ty)"; skipped=$((skipped+1)); continue; }
    cls="$(classify "$p")"
    case "$cls" in
        UNCLASSIFIED) echo "FAIL $p (unclassified in wine_corelib.sh -- classify it)"; fail=$((fail+1)); continue ;;
        skip*) echo "skip $p (${cls#skip })"; skipped=$((skipped+1)); continue ;;
        run) LIB="" ;;
        run-*) LIB="${cls#run-}" ;;
    esac
    $E "$d/main.ty" --emit-c -o "$T/$p" >"$T/$p.emit" 2>&1 || { echo "FAIL $p (emit)"; fail=$((fail+1)); continue; }
    SHIMS=$($E "$d/main.ty" --print-shims 2>/dev/null | sed 's|^Z:||; s|\\|/|g' | tr '\n' ' ')
    "$MINGWCC" -O3 -fwrapv -pthread -o "$T/$p.exe" "$T/$p.c" $SHIMS $LIB -lm 2>"$T/$p.cc" \
        || { echo "FAIL $p (cc)"; head -2 "$T/$p.cc" | sed 's/^/      /'; fail=$((fail+1)); continue; }
    # -k: wine spawns a process tree (wineserver + the test); a plain timeout
    # would kill the client and leave the server hanging the lane
    ( cd "$(pwd)" && timeout -k 5 120 $W "$T/$p.exe" 2>&1 ) >"$T/$p.out"
    # A line where WINDOWS IS CORRECT to disagree with the Linux golden. Written
    # as a sed rewriting the Windows value back to the Linux one, so it fires only
    # for the exact documented answer -- any OTHER value leaves the line alone and
    # the lane red. That is what separates this from ignoring the line: a real
    # regression on the same line still fails.
    #
    # Not a package `skip`: io's other 42 lines are byte-identical under Wine and
    # skipping the package to excuse one line throws that coverage away.
    got="$T/$p.out"; xnote=""
    if ! cmp -s "$got" "corelib/test/$p.out"; then
        case $p in
            # Windows has no directory fsync. corelib/io/io.ty documents
            # Err(Unsupported) as the DESIGNED answer for that case, and keeps it
            # distinct from Err(Failed) precisely so a platform that cannot flush
            # is not confused with one that tried and lied. The Linux `sync_dir=Ok`
            # is therefore unreachable there, and `Failed` would still redden.
            io) xsed='s/^sync_file=Ok sync_dir=Unsupported /sync_file=Ok sync_dir=Ok /'
                xwhy='sync_dir: no directory fsync on Windows, io.ty documents Unsupported' ;;
            *)  xsed=''; xwhy='' ;;
        esac
        if [ -n "$xsed" ] && sed "$xsed" "$got" > "$T/$p.win" && cmp -s "$T/$p.win" "corelib/test/$p.out"; then
            got="$T/$p.win"; xnote=", after 1 documented Windows difference -- $xwhy"
        fi
    fi
    if cmp -s "$got" "corelib/test/$p.out"; then
        echo "ok    $p (byte-identical under Wine$xnote)"
        pass=$((pass+1))
    else
        echo "FAIL $p (output differs)"; diff "corelib/test/$p.out" "$T/$p.out" | head -6 | sed 's/^/      /'
        fail=$((fail+1))
    fi
done

echo
echo "wine-corelib: passed $pass  failed $fail  skipped $skipped"
[ "$fail" -eq 0 ] || { echo "wine-corelib: FAIL ($fail)"; exit 1; }
echo "wine-corelib: all green (approximation only)"
