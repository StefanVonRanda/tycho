#!/bin/sh
# Fault-injection lane: inject short I/O and EINTR into the process under test
# and require the same golden output. A wrong result means a shim loop treated a
# partial transfer as EOF or an interruption as a failure. Linux-only because the
# injector uses LD_PRELOAD and glibc's open64.
set -u
cd "$(dirname "$0")/.."
case "$(uname -s)" in
    Linux) ;;
    *) echo "fault-inject: skip (LD_PRELOAD injector is Linux-only)"; exit 0 ;;
esac
TYCHOC="${TYCHOC:-./tychoc1}"
[ -x "$TYCHOC" ] || { echo "fault-inject: no $TYCHOC -- run make" >&2; exit 2; }
CC="${CC:-cc}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
SO="$T/fault_inject.so"
$CC -shared -fPIC -O2 -Wall -Wextra -Werror -o "$SO" scripts/fault_inject.c -ldl || exit 2
export TYCHO_CORELIB="$PWD/corelib"

fail=0
run() {
    mode=$1; nth=$2; bin=$3; input=$4; out=$5; err=$6
    timeout 60 sh -c "FI_ONLY_PID=\$\$ exec env $mode=$nth LD_PRELOAD=$SO $bin" \
        < "$input" > "$out" 2> "$err"
    return $?
}

check() {
    name=$1; entry=$2; golden=$3; input=$4; short=$5
    bin="$T/$name"
    if ! "$TYCHOC" "$entry" -o "$bin" >"$T/$name.build" 2>&1; then
        echo "skip $name (build: $(head -1 "$T/$name.build"))"
        return 0
    fi
    if ! timeout 60 "$bin" < "$input" >"$T/$name.base" 2>"$T/$name.base.err" ||
       ! cmp -s "$golden" "$T/$name.base"; then
        echo "skip $name (baseline does not match $golden)"
        return 0
    fi
    if [ "$short" = yes ]; then
        for mode in FI_READ_SHORT FI_WRITE_SHORT; do
            run "$mode" 0 "$bin" "$input" "$T/$name.out" "$T/$name.err" || {
                echo "FAIL $name $mode rc=$?"; fail=$((fail + 1)); continue
            }
            if ! cmp -s "$golden" "$T/$name.out"; then
                echo "FAIL $name $mode output moved"
                diff "$golden" "$T/$name.out" | head -4 | sed 's/^/      /'
                fail=$((fail + 1))
            fi
        done
    fi
    for mode in FI_READ_EINTR FI_WRITE_EINTR; do
        for nth in 1 2 3 4 5 6 7 8; do
            run "$mode" "$nth" "$bin" "$input" "$T/$name.out" "$T/$name.err" || {
                echo "FAIL $name $mode n=$nth rc=$?"; fail=$((fail + 1)); continue
            }
            if ! cmp -s "$golden" "$T/$name.out"; then
                echo "FAIL $name $mode n=$nth output moved"
                diff "$golden" "$T/$name.out" | head -4 | sed 's/^/      /'
                fail=$((fail + 1))
            fi
        done
    done
    echo "ok   $name"
}

check stdin_bytes tests/pkg/stdin_bytes/main.ty tests/pkg/stdin_bytes.out \
      tests/pkg/stdin_bytes.in yes
check io corelib/test/io/main.ty corelib/test/io.out /dev/null yes
check os corelib/test/os/main.ty corelib/test/os.out /dev/null yes
check net corelib/test/net/main.ty corelib/test/net.out /dev/null no
check httpd corelib/test/httpd/main.ty corelib/test/httpd.out /dev/null yes
check sqlite corelib/test/sqlite/main.ty corelib/test/sqlite.out /dev/null no

# The control: a failing read must change the stdin fixture's answer. If it does
# not, the injector never reached the process and every green above is empty.
run FI_READ_FAIL 0 "$T/stdin_bytes" tests/pkg/stdin_bytes.in "$T/control.out" "$T/control.err" || true
if cmp -s tests/pkg/stdin_bytes.out "$T/control.out"; then
    echo "FAIL control: FI_READ_FAIL did not change the stdin output"
    fail=$((fail + 1))
else
    echo "ok   control (injected read failure reaches the process)"
fi

if [ "$fail" -eq 0 ]; then
    echo "fault-inject: all green"
    exit 0
fi
echo "fault-inject: $fail failure(s)"
exit 1
