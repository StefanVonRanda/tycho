set -eu

CC="${CC:-cc}"
fail=0 ok=0 skipped=0

for shim in corelib/*/*_shim.c; do
    dir="$(dirname "$shim")"
    depflags=""

    if [ -f "$dir/deps" ]; then
        pkgs="$(grep -vE '^[[:space:]]*(#|$)' "$dir/deps" || true)"
        missing=""
        for pkg in $pkgs; do
            pkg-config --exists "$pkg" 2>/dev/null || missing="$missing $pkg"
        done
        if [ -n "$missing" ]; then
            echo "skip $shim (missing dependency:$missing)"
            skipped=$((skipped + 1))
            continue
        fi
        depflags="$(pkg-config --cflags $pkgs 2>/dev/null || true)"
    fi

    if out="$($CC -std=c11 -fsyntax-only $depflags "$shim" 2>&1)"; then
        echo "ok   $shim"
        ok=$((ok + 1))
    else
        echo "FAIL $shim"
        echo "$out" | sed 's/^/       /'
        fail=$((fail + 1))
    fi
done

QC=corelib/os/os_argv_quotecheck.c
if [ -f "$QC" ]; then
    case "$(uname -s)" in
        *MSYS*|*MINGW*|*CYGWIN*)
            if qout="$($CC -std=c11 -Wall -Wextra -I corelib/os "$QC" -o "${TMPDIR:-/tmp}/os_argv_quotecheck.exe" -lshell32 2>&1)" \
               && qout="$("${TMPDIR:-/tmp}/os_argv_quotecheck.exe" 2>&1)"; then
                echo "ok   $QC (argv round-trips CommandLineToArgvW)"
                ok=$((ok + 1))
            else
                echo "FAIL $QC"
                echo "$qout" | sed 's/^/       /'
                fail=$((fail + 1))
            fi
            ;;
        *)
            echo "skip $QC (Windows-only: needs CommandLineToArgvW)"
            skipped=$((skipped + 1))
            ;;
    esac
fi

echo "shim-check: $ok ok, $skipped skipped, $fail failed"
[ "$fail" -eq 0 ] || {
    echo "shim-check: a shim does not compile standalone under -std=c11." >&2
    echo "  Fix it in the shim, not here: declare the feature-test macro it needs" >&2
    echo "  before its first #include, the way corelib/io/io_shim.c does." >&2
    exit 1
}
