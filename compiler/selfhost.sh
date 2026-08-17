set -u
cd "$(dirname "$0")/.." || exit 2
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "selfhost: no ./tychoc -- run 'make' first"; exit 2; }
H=compiler/tychoc0.ty
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
bad() { echo "FAIL: $*"; fail=1; }

case "$(uname -s)" in *MSYS*|*MINGW*|*CYGWIN*)
    echo "selfhost: SKIP (Windows: frozen tychoc0 emits POSIX-only C -- compiler/tychoc0.ty:10688 has no _WIN32 tycho_ncpu; un-freezing it is an owner decision)"
    exit 0 ;;
esac

if ! "$TYCHOC" "$H" -o "$T/A" >"$T/build.log" 2>&1; then
    echo "FAIL: tychoc could not build compiler/tychoc0.ty"; sed 's/^/      /' "$T/build.log"
    echo "selfhost: FAIL"; exit 1
fi

# stage B: the Tycho-built compiler emits its own C
if ! "$T/A" "$H" > "$T/B.c" 2>"$T/A.err"; then
    bad "stage B: the Tycho-built compiler failed to compile its own source"
    sed 's/^/      /' "$T/A.err"
fi
if ! cc -O2 -fwrapv -std=c11 -o "$T/B" "$T/B.c" -lm 2>"$T/cc.err"; then
    bad "stage B cc: the emitted C failed to compile"
    sed 's/^/      /' "$T/cc.err"
fi

# stage C: the second-generation compiler emits its own C again
if [ "$fail" -eq 0 ]; then
    if ! "$T/B" "$H" > "$T/C.c" 2>"$T/B.err"; then
        bad "stage C: the second-generation compiler failed to compile its own source"
        sed 's/^/      /' "$T/B.err"
    fi
fi

# the fixed point: the two emissions are byte-identical
if [ "$fail" -eq 0 ]; then
    if cmp -s "$T/B.c" "$T/C.c"; then
        echo "selfhost: green (tychoc0 compiled by itself emits byte-identical C -- the fixed point holds at HEAD)"
    else
        bad "the fixed point broke: A(tychoc0.ty) != B(tychoc0.ty)"
        diff "$T/B.c" "$T/C.c" | head -5 | sed 's/^/      /'
    fi
fi

if [ "$fail" -ne 0 ]; then
    echo "selfhost: FAIL"; exit 1
fi
