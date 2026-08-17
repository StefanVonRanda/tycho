set -u
cd "$(dirname "$0")/.." || exit 2
MINGWCC="$(command -v x86_64-w64-mingw32-gcc || true)"
WINE="$(command -v wine64 || command -v wine || true)"
[ -n "$WINE" ] || { echo "SKIP: neither wine64 nor wine on PATH"; exit 0; }
[ -n "$MINGWCC" ] || { echo "SKIP: x86_64-w64-mingw32-gcc not on PATH"; exit 0; }
export LD_PRELOAD=
FILTER="${WINE_CORELIB_FILTER:-}"

T="$(mktemp -d)"; trap 'rm -rf "$T"; pkill -f "wine.*/tmp/tmp\." 2>/dev/null; pkill wineserver 2>/dev/null' EXIT
fail=0; pass=0; skipped=0

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
    "$MINGWCC" ${WINE_CCF:--O3 -fwrapv -pthread} -o "$T/$p.exe" "$T/$p.c" $SHIMS $LIB -lm 2>"$T/$p.cc" \
        || { echo "FAIL $p (cc)"; head -2 "$T/$p.cc" | sed 's/^/      /'; fail=$((fail+1)); continue; }
    # -k: wine spawns a process tree (wineserver + the test); a plain timeout
    # would kill the client and leave the server hanging the lane
    ( cd "$(pwd)" && timeout -k 5 120 $W "$T/$p.exe" 2>&1 ) >"$T/$p.out"
    got="$T/$p.out"; xnote=""
    if ! cmp -s "$got" "corelib/test/$p.out"; then
        case $p in
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
