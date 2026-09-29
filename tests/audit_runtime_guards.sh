#!/bin/sh
# Regressions for audited size guards and regex match/group bounds in both emitters.
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
check_abort() {
    compiler=$1 source=$2 expected=$3 name=$4
    "$compiler" "$source" -o "$tmp/$name" >"$tmp/$name.build" 2>&1 || {
        echo "audit-runtime-guards: $compiler failed to build $source"; cat "$tmp/$name.build"; exit 1;
    }
    set +e
    "$tmp/$name" >"$tmp/$name.out" 2>"$tmp/$name.err"
    rc=$?
    set -e
    [ "$rc" -ne 0 ] && cmp -s "$expected" "$tmp/$name.err" || {
        echo "audit-runtime-guards: $compiler $source returned $rc or wrong diagnostic";
        cat "$tmp/$name.err"; exit 1;
    }
}
for compiler in ./tychoc ./tychoc1; do
    check_abort "$compiler" tests/abort/reserve_composite_overflow.ty tests/abort/reserve_composite_overflow.err reserve
    check_abort "$compiler" tests/abort/channel_capacity_overflow.ty tests/abort/channel_capacity_overflow.err channel
    check_abort "$compiler" tests/fixtures/audit_regex_subject_limit/main.ty tests/regex_subject_limit.err regex_limit
    "$compiler" tests/fixtures/audit_regex_group_large/main.ty -o "$tmp/regex_group" >"$tmp/regex_group.build" 2>&1 || {
        echo "audit-runtime-guards: $compiler failed to build regex group fixture"; cat "$tmp/regex_group.build"; exit 1;
    }
    [ "$("$tmp/regex_group")" = "-1" ] || {
        echo "audit-runtime-guards: $compiler accepted an out-of-range regex group"; exit 1;
    }
done
echo "audit-runtime-guards: reserve, channel and regex bounds hold in both compilers"
