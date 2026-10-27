#!/bin/sh
# Descriptor and syscall failure lane (Linux and macOS).
set -u
cd "$(dirname "$0")/.."
case "$(uname -s)" in
    Linux)  PRELOAD=LD_PRELOAD; SOFLAGS="-shared -fPIC"; SOEXT=so; LDLIB=-ldl ;;
    Darwin) PRELOAD=DYLD_INSERT_LIBRARIES; SOFLAGS="-dynamiclib"; SOEXT=dylib; LDLIB= ;;
    *) echo "descriptor-fail: skip (no syscall interposer for this platform)"; exit 0 ;;
esac
CC="${CC:-cc}"
TYCHOC="${TYCHOC:-./tychoc1}"
if command -v timeout >/dev/null 2>&1; then TO=timeout
elif command -v gtimeout >/dev/null 2>&1; then TO=gtimeout
else echo "${0##*/}: skip (no timeout/gtimeout)"; exit 0; fi
[ -x "$TYCHOC" ] || { echo "descriptor-fail: no $TYCHOC -- run make" >&2; exit 2; }
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
SO="$T/fault_inject.$SOEXT"
$CC $SOFLAGS -O2 -Wall -Wextra -Werror -o "$SO" scripts/fault_inject.c $LDLIB || exit 2
"$TYCHOC" scripts/probes/descriptor/main.ty -o "$T/probe" >"$T/build" 2>&1 || {
    echo "descriptor-fail: probe build failed"; head -3 "$T/build" | sed 's/^/      /'; exit 1
}
"$TYCHOC" scripts/probes/fd_exhaust/main.ty -o "$T/fdex" >"$T/fdbuild" 2>&1 || {
    echo "descriptor-fail: fd probe build failed"; head -3 "$T/fdbuild" | sed 's/^/      /'; exit 1
}
export TYCHO_CORELIB="$PWD/corelib"

prepare() {
    rm -rf /tmp/tycho_desc_dir /tmp/tycho_desc_dst /tmp/tycho_desc_write \
           /tmp/tycho_desc_at /tmp/tycho_desc_remove
    printf 'src\n' > /tmp/tycho_desc_src
    printf 'remove\n' > /tmp/tycho_desc_remove
}

run() {
    mode=$1
    $TO 30 sh -c "FI_ONLY_PID=\$\$ exec env $mode $PRELOAD=$SO $T/probe" \
        >"$T/out" 2>"$T/err"
    return $?
}

expect() {
    grep -q "^$1$" "$T/out" || {
        echo "FAIL $2: expected '$1', got '$(grep "^${1%%=*}=" "$T/out" | head -1)'"
        return 1
    }
}

check_fds() {
    line=$(grep '^fds=' "$T/out" | head -1)
    [ -n "$line" ] || { echo "FAIL $1: no fds line"; return 1; }
    vals=${line#fds=}
    a=${vals%%,*}; rest=${vals#*,}; b=${rest%%,*}; c=${rest#*,}
    [ "$a" = "$b" ] && [ "$b" = "$c" ] || {
        echo "FAIL $1: descriptor count grew ($vals)"; return 1
    }
}

fail=0
prepare
if ! run ""; then echo "FAIL baseline rc=$?"; fail=1; else
    for line in 'read=Ok' 'write=Ok' 'at=Ok' 'sync=Ok' 'rename=Ok' \
                'mkdir=Ok' 'remove=Ok' 'list=Ok' 'lines=Ok' 'cap=0,0,0' \
                'listen=Ok' 'connect=Err' 'poll=Timeout' 'accept=Ok'; do
        expect "$line" baseline || fail=1
    done
    check_fds baseline || fail=1
    [ "$fail" -eq 0 ] && echo "ok   baseline"
fi

check_case() {
    name=$1; mode=$2; line=$3; fds=$4
    prepare
    if ! run "$mode"; then
        echo "FAIL $name rc=$?"; fail=1; return
    fi
    for want in $(printf '%s' "$line" | tr '|' ' '); do
        expect "$want" "$name" || { fail=1; return; }
    done
    if [ "$fds" = yes ]; then check_fds "$name" || fail=1; fi
    echo "ok   $name"
}

check_case fopen          FI_FOPEN_FAIL=0     'read=Err|write=Err|lines=Err' no
check_case opendir        FI_OPENDIR_FAIL=0   'list=Err'        no
check_case open           FI_OPEN_FAIL=0       'at=Err'          no
check_case fsync          FI_FSYNC_FAIL=0      'sync=Err'        no
check_case rename         FI_RENAME_FAIL=0     'rename=Err'      no
check_case mkdir          FI_MKDIR_FAIL=0      'mkdir=Err'       no
check_case remove         FI_REMOVE_FAIL=0     'remove=Err'      no
check_case pipe-first     FI_PIPE_FAIL=0       'cap=-1,0,0'      yes
check_case pipe-second    FI_PIPE_EVERY=2      'cap=-1,0,0'      yes
check_case socket         FI_SOCKET_FAIL=0     'listen=Err'      yes
check_case connect        FI_CONNECT_FAIL=0    'connect=Err'     yes
check_case accept         FI_ACCEPT_FAIL=0     'accept=Err'      yes
check_case poll           FI_POLL_FAIL=0       'poll=Err'        yes

( ulimit -n 16; $TO 30 "$T/fdex" ) >"$T/fdout" 2>"$T/fderr"
rc=$?
if [ "$rc" -ne 0 ]; then
    echo "FAIL fd-exhaust rc=$rc"; head -3 "$T/fderr" | sed 's/^/      /'; fail=1
else
    line=$(grep '^ex=' "$T/fdout" | head -1)
    vals=${line#ex=}
    held=${vals%%,*}; rest=${vals#*,}
    exread=${rest%%,*}; rest=${rest#*,}
    cap=${rest%%,*}; rest=${rest#*,}
    listen=${rest%%,*}; rest=${rest#*,}
    recovered=${rest%%,*}; rest=${rest#*,}
    before=${rest%%,*}; after=${rest#*,}
    if [ -z "$line" ] || [ "$held" -le 0 ] || [ "$exread" != Err ] ||
       [ "$cap" != -1 ] || [ "$listen" != Err ] || [ "$recovered" != Ok ] ||
       [ "$before" != "$after" ]; then
        echo "FAIL fd-exhaust: $line"; fail=1
    else
        echo "ok   fd-exhaust ($line)"
    fi
fi

if [ "$fail" -eq 0 ]; then
    echo "descriptor-fail: all green"
    exit 0
fi
echo "descriptor-fail: $fail failure(s)"
exit 1
