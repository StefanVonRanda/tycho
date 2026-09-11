#!/bin/sh
# What THIS MACHINE can run, measured rather than read. `make preflight`.
#
# WHY THIS EXISTS. On 2026-09-11 `make ci` was run on a box that had never run
# it, and it took five separate rounds of diagnosis to get a verdict: absent
# sanitizer runtimes failed 335 fixtures at the LINK step (a suite that looks
# catastrophically broken when nothing is wrong), a missing static libc took out
# tychoc1 and then, separately, the three shipped tools through a different flag
# variable, and a missing 32-bit toolchain failed a lane that correctly refuses
# to skip. None of those says what it wants. Each is one package.
#
# CONTRIBUTING.md now names them in prose. Prose is read once and drifts; this
# RUNS every check, so a reader learns what their box is missing in one command
# instead of five failures. Nothing here is read from a document.
#
# Exit 0 if every REQUIRED check holds -- optional ones only make lanes skip.
set -u
cd "$(dirname "$0")/.." || exit 2
CC="${CC:-cc}"

# --selfcheck: prove the legs can FAIL. A preflight that prints ok unconditionally
# is worse than none -- it certifies a box it never probed.
if [ "${1:-}" = "--selfcheck" ]; then
    sc=0
    # [1] every compiler-based required leg must report MISSING when cc cannot run.
    out="$(CC=/bin/false sh "$0" 2>&1)"; rc=$?
    n="$(printf '%s\n' "$out" | grep -c 'MISSING')"
    if [ "$n" -ge 5 ] && [ "$rc" -ne 0 ]; then
        echo "  [1] cc that always fails -> $n MISSING, exit $rc   ok"
    else
        echo "  [1] cc that always fails -> $n MISSING, exit $rc   FAIL (want >=5 and non-zero)"; sc=1
    fi
    # [2] a real cc must reach a verdict and say so on the last line.
    out="$(sh "$0" 2>&1)"; rc=$?
    if printf '%s\n' "$out" | tail -1 | grep -q '^preflight: '; then
        echo "  [2] a real cc reaches a verdict line                ok"
    else
        echo "  [2] a real cc reaches a verdict line                FAIL"; sc=1
    fi
    # [3] the probe must carry a .rodata relocation. An empty main links under
    #     -static-pie WITHOUT -fPIE on toolchains where this repo does NOT build,
    #     so a probe using one reports a false ok. Assert the difference exists
    #     where it is observable, and say so where it is not.
    D="$(mktemp -d)"
    printf 'int main(void){return 0;}\n' > "$D/empty.c"
    printf '#include <stdio.h>\nint main(void){ puts("x"); return 0; }\n' > "$D/lit.c"
    if "${CC:-cc}" -static-pie "$D/empty.c" -o "$D/e" 2>/dev/null &&
       ! "${CC:-cc}" -static-pie "$D/lit.c" -o "$D/l" 2>/dev/null; then
        echo "  [3] empty main links, string literal does not       ok (the false green is real here)"
    else
        echo "  [3] this toolchain does not show the -fPIE split    n/a (both forms agree; the probe still uses the literal)"
    fi
    rm -rf "$D"
    [ "$sc" -eq 0 ] && echo "preflight selfcheck: ok" || echo "preflight selfcheck: FAILED"
    exit "$sc"
fi
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT INT TERM
req_bad=0; opt_bad=0

say() { printf '  %-22s %s\n' "$1" "$2"; }
need() { # need <label> <fedora-pkg> <what it costs you>  -- reads $ok
    if [ "$ok" = 1 ]; then say "$1" "ok"; else
        say "$1" "MISSING -- $3"; printf '  %-22s   fedora: %s\n' "" "$2"; req_bad=$((req_bad+1)); fi
}
opt() {
    if [ "$ok" = 1 ]; then say "$1" "ok"; else
        say "$1" "absent -- $3"; printf '  %-22s   fedora: %s\n' "" "$2"; opt_bad=$((opt_bad+1)); fi
}

# A real main with a STRING LITERAL, not `int main(void){return 0;}`. The empty
# program has no .rodata relocation, so it links under -static-pie whether or not
# -fPIE reached the compile step -- it reports success on a box that cannot build
# this repo. That false green cost a round of diagnosis; the literal is the fix.
cat > "$T/p.c" <<'EOF'
#include <stdio.h>
int main(void){ puts("x"); return 0; }
EOF

echo "required:"
$CC "$T/p.c" -o "$T/a" 2>/dev/null && ok=1 || ok=0
need "c compiler ($CC)" "gcc" "nothing builds"

command -v make >/dev/null 2>&1 && ok=1 || ok=0
need "make" "make" "nothing builds"

command -v python3 >/dev/null 2>&1 && ok=1 || ok=0
need "python3" "python3" "server-check and several gates SKIP or fail"

$CC -fsanitize=address,undefined "$T/p.c" -o "$T/a" 2>/dev/null && ok=1 || ok=0
need "asan + ubsan" "libasan libubsan" "make test fails ~335 fixtures at the LINK step"

$CC -fsanitize=thread "$T/p.c" -o "$T/a" 2>/dev/null && ok=1 || ok=0
need "tsan" "libtsan" "the concurrency lane cannot build"

$CC -static-pie -fPIE "$T/p.c" -o "$T/a" 2>/dev/null && ok=1 || ok=0
need "static libc" "glibc-static" "tychoc1 and the three shipped tools cannot link"

$CC -m32 "$T/p.c" -o "$T/a" 2>/dev/null && ok=1 || ok=0
need "32-bit toolchain" "glibc-devel.i686 libgcc.i686 libatomic.i686" "the ilp32 lane fails (it refuses to skip)"

echo "optional -- absent only makes a lane skip:"
pkg-config --exists sqlite3 2>/dev/null && ok=1 || ok=0
opt "sqlite3" "sqlite-devel" "the sqlite FFI example and one docs fence skip"
pkg-config --exists libcurl 2>/dev/null && ok=1 || ok=0
opt "libcurl" "libcurl-devel" "core:http and its example skip"
pkg-config --exists libpng 2>/dev/null && ok=1 || ok=0
opt "libpng" "libpng-devel" "core:image and its example skip"
command -v gdb >/dev/null 2>&1 && ok=1 || ok=0
opt "gdb" "gdb" "the debugging.md transcript cannot run"
command -v npx >/dev/null 2>&1 && ok=1 || ok=0
opt "npx" "nodejs-npm" "the zed editor check skips"
{ command -v wine64 >/dev/null 2>&1 || command -v wine >/dev/null 2>&1; } && ok=1 || ok=0
opt "wine" "wine" "release-content and the wine lanes skip"
command -v go >/dev/null 2>&1 && ok=1 || ok=0
opt "go" "golang" "the cross-language benchmarks skip"

echo
if [ "$req_bad" -eq 0 ]; then
    echo "preflight: ok -- every required check holds ($opt_bad optional absent)"
else
    echo "preflight: $req_bad REQUIRED check(s) missing -- \`make ci\` will fail for that reason, not yours"
fi
[ "$req_bad" -eq 0 ]
