set -u
cd "$(dirname "$0")/.." || exit 2

# glibc-check: what libc floor does a program built by THIS tree carry?
#
# The driver's cc line sets no -std (compiler/driver/driver.ty@argv), so on a
# C23-default host glibc redirects strtol to __isoc23_strtol@GLIBC_2.38 and every
# Tycho binary silently stops starting on Rocky 9 (2.34), Ubuntu 22.04 (2.35) and
# Debian 12 (2.36). Nothing here could see that: the program builds, runs on the
# build host and matches its golden. Only the SYMBOL VERSIONS say it.
FLOOR="${TYCHO_GLIBC_FLOOR:-2.34}"     # Rocky 9, the oldest distro we claim
TYCHOC="${TYCHOC:-./tychoc1}"
CC="${CC:-cc}"
D=""
cleanup() { [ -n "$D" ] && rm -rf "$D"; }
trap cleanup EXIT

# max_glibc <binary> -- the highest GLIBC_x.y version any undefined symbol needs.
max_glibc() {
    objdump -T "$1" 2>/dev/null | grep -o 'GLIBC_[0-9][0-9.]*' | sed 's/^GLIBC_//' \
        | sort -uV | tail -1
}
# above_floor <binary> -- every "sym@GLIBC_x.y" whose version exceeds $FLOOR.
above_floor() {
    nm -D --undefined-only "$1" 2>/dev/null | grep -o '[A-Za-z_][A-Za-z0-9_]*@GLIBC_[0-9][0-9.]*' \
    | while IFS= read -r s; do
        v="${s##*@GLIBC_}"
        [ "$v" = "$FLOOR" ] && continue
        [ "$(printf '%s\n%s\n' "$v" "$FLOOR" | sort -V | tail -1)" = "$v" ] && echo "$s"
      done
}

command -v objdump >/dev/null 2>&1 && command -v nm >/dev/null 2>&1 || {
    echo "glibc-check: SKIP (no objdump/nm on this host -- nothing can read symbol versions)"; exit 0; }

# --selfcheck: prove the two scanners can FIRE, on a pair that must disagree.
# A grep that silently stopped matching is indistinguishable from a clean tree.
if [ "${1-}" = "--selfcheck" ]; then
    D=$(mktemp -d) || exit 2
    printf '#define _GNU_SOURCE\n#include <stdlib.h>\nint main(int c,char**v){(void)c;return (int)strtol(v[0],0,10);}\n' > "$D/dirty.c"
    printf '#include <stdlib.h>\nint main(int c,char**v){(void)c;(void)v;return (int)atoi("1");}\n' > "$D/clean.c"
    "$CC" -std=gnu23 -o "$D/dirty" "$D/dirty.c" 2>/dev/null || {
        echo "glibc-check: SKIP (this $CC cannot build the -std=gnu23 control -- nothing to detect)"; exit 0; }
    "$CC" -std=gnu17 -o "$D/clean" "$D/clean.c" || { echo "selfcheck: clean control did not build"; exit 1; }
    dv=$(above_floor "$D/dirty"); cv=$(above_floor "$D/clean")
    case "$dv" in *__isoc23_strtol*) ;; *)
        echo "selfcheck: CONTROL DEAD -- a program that calls strtol at -std=gnu23 was not flagged (got: [$dv]); the scanner sees nothing and every verdict below is decoration"; exit 1;; esac
    [ -z "$cv" ] || { echo "selfcheck: FALSE POSITIVE -- the clean control was flagged: $cv"; exit 1; }
    echo "glibc-check --selfcheck: ok (dirty flagged $dv, clean flagged nothing, floor $FLOOR)"
    exit 0
fi

[ -x "$TYCHOC" ] || { echo "glibc-check: no $TYCHOC -- run 'make' first" >&2; exit 2; }
D=$(mktemp -d) || exit 2

# A REPRESENTATIVE program, not hello-world: the defect arrived through the
# runtime, which every program embeds, and the lane must also link a shim.
cat > "$D/main.ty" <<'TY'
package main
import "core:strings"

fn main():
    n := strings.parse_int("41")
    c := strings.parse_int_checked("1234")
    f := strings.parse_float("2.5")
    u := strings.to_upper("floor")
    println(f"{u} {n + 1} {c} {f}")
TY
"$TYCHOC" "$D/main.ty" -o "$D/prog" >"$D/build.log" 2>&1 || {
    echo "glibc-check: FAILED [1] the representative program did not build"; cat "$D/build.log"; exit 1; }

# [1] It must RUN and print the right answer. A binary that merely links proves
# nothing about a floor, and a broken build would make [2]/[3] vacuously clean.
want="FLOOR 42 Ok(1234) Ok(2.5)"
got=$("$D/prog" 2>&1) || { echo "glibc-check: FAILED [1] the program did not run: $got"; exit 1; }
[ "$got" = "$want" ] || { echo "glibc-check: FAILED [1] wrong answer: [$got] want [$want]"; exit 1; }

# [2] No __isoc23_* at all. Named on its own because it is the whole family --
# strtol/strtoul/strtoll/strtoull/strtoq/strtouq and their _l forms, the
# strtoimax/strtoumax pair, scanf/fscanf/sscanf/vscanf/vfscanf/vsscanf and every
# wide sibling -- and any one of them lands the binary at GLIBC_2.38.
iso=$(nm -D --undefined-only "$D/prog" 2>/dev/null | grep -o '__isoc23_[A-Za-z0-9_]*' | sort -u)
[ -z "$iso" ] || { echo "glibc-check: FAILED [2] C23-redirected symbol(s): $iso"; exit 1; }

# [3] The floor itself, which is the claim a user cares about.
hi=$(max_glibc "$D/prog")
bad=$(above_floor "$D/prog")
[ -z "$bad" ] || {
    echo "glibc-check: FAILED [3] symbols above GLIBC_$FLOOR: $bad"; exit 1; }

# [4] The fix replaced the runtime's strtol with a hand-rolled digit scan, so
# TYCHO_BLOCK is now parsed by code this tree owns. A knob that quietly stopped
# working reads exactly like one that works.
a=$(TYCHO_ARENA_STATS=1 "$D/prog" 2>&1 | grep 'OS reserved')
b=$(TYCHO_BLOCK=1048576 TYCHO_ARENA_STATS=1 "$D/prog" 2>&1 | grep 'OS reserved')
[ -n "$a" ] && [ "$a" != "$b" ] || {
    echo "glibc-check: FAILED [4] TYCHO_BLOCK moved nothing: default [$a] override [$b]"; exit 1; }

echo "glibc-check: all green (4 legs) -- max GLIBC_$hi, floor GLIBC_$FLOOR, no __isoc23_*"
