set -eu

# One spelling of the `deps` parse, shared with scripts/shim_warn.sh.
. "$(dirname "$0")/deps_pkgs.sh"

CC="${CC:-cc}"
fail=0 ok=0 skipped=0

for shim in corelib/*/*_shim.c; do
    dir="$(dirname "$shim")"
    depflags=""

    if [ -f "$dir/deps" ]; then
        pkgs="$(pkgs_of "$dir/deps")"
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

    if out="$($CC -std=c11 -fsyntax-only -Icorelib $depflags "$shim" 2>&1)"; then
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

BC=corelib/os/os_batch_check.c
if [ -f "$BC" ]; then
    case "$(uname -s)" in
        *MSYS*|*MINGW*|*CYGWIN*|*Windows*)
            echo "skip $BC (already scored by os_argv_quotecheck + os_shim.c)"
            skipped=$((skipped + 1))
            ;;
        *)
            bc_bin="${TMPDIR:-/tmp}/os_batch_check"
            bc_cc=""; bc_rc=0
            bc_cc="$($CC -std=c11 -Wall -Wextra -o "$bc_bin" "$BC" 2>&1)" || bc_rc=$?
            if [ "$bc_rc" -ne 0 ]; then
                echo "FAIL $BC"
                echo "$bc_cc" | sed 's/^/       /'
                fail=$((fail + 1))
            else
                bc_run=""; bc_rrc=0
                bc_run="$($bc_bin 2>&1)" || bc_rrc=$?
                if [ "$bc_rrc" -ne 0 ]; then
                    echo "FAIL $BC"
                    echo "$bc_cc" | sed 's/^/       /'
                    echo "$bc_run" | sed 's/^/       /'
                    fail=$((fail + 1))
                else
                    echo "ok   $BC (osx_is_batch suffix logic scored natively)"
                    echo "       $bc_run"
                    ok=$((ok + 1))
                fi
            fi
            ;;
    esac
fi

# [extern] A package's native dependency does not have to arrive through a shim:
# core:sqlite binds libsqlite3 with `extern "sqlite3"` and has no shim at all, so
# the loop above cannot see it. Every `extern "<lib>"` name in corelib must be
# declared in its package's `deps`, or `--print-deps` reports nothing for it and
# the corelib harness FAILS instead of SKIPPING where the library is absent.
nextern=0
for pkgdir in corelib/*/; do
    libs="$(cat "$pkgdir"*.ty 2>/dev/null | sed -n 's/.*extern "\([A-Za-z0-9_.+-]*\)".*/\1/p' | sort -u)"
    [ -n "$libs" ] || continue
    declared=""
    [ -f "$pkgdir/deps" ] && declared="$(pkgs_of "$pkgdir/deps")"
    for lib in $libs; do
        nextern=$((nextern + 1))
        found=0
        for d in $declared; do [ "$d" = "$lib" ] && found=1; done
        if [ "$found" -eq 1 ]; then
            echo "ok   ${pkgdir}deps declares extern \"$lib\""
            ok=$((ok + 1))
        else
            echo "FAIL ${pkgdir}deps does not declare extern \"$lib\""
            fail=$((fail + 1))
        fi
    done
done
# The scan is only as good as its pattern: a sed that silently stops matching is
# indistinguishable from a tree with no externs left.
if [ "$nextern" -eq 0 ]; then
    echo "FAIL extern scan found 0 `extern \"lib\"` declarations in corelib -- the pattern stopped matching"
    fail=$((fail + 1))
fi

echo "shim-check: $ok ok, $skipped skipped, $fail failed"
[ "$fail" -eq 0 ] || {
    echo "shim-check: a shim does not compile standalone under -std=c11, or a" >&2
    echo "  package's extern library is in no deps file. Read the FAIL lines above." >&2
    echo "  A shim: declare the feature-test macro it needs before its first" >&2
    echo "  #include, the way corelib/io/io_shim.c does." >&2
    echo "  An extern: add the pkg-config name to corelib/<pkg>/deps, or the" >&2
    echo "  corelib harness fails instead of skipping where the library is absent." >&2
    exit 1
}
