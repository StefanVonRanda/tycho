#!/bin/sh
# Regressions for audited size guards and regex match/group bounds in both emitters.
set -eu
cd "$(dirname "$0")/.."
. ./scripts/shlib.sh
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
check_abort() {
    compiler=$1 source=$2 expected=$3 name=$4
    "$compiler" "$source" -o "$tmp/$name" >"$tmp/$name.build" 2>&1 || {
        echo "audit-runtime-guards: $compiler failed to build $source"; cat "$tmp/$name.build"; exit 1;
    }
    set +e
    timeout 10 "$tmp/$name" >"$tmp/$name.out" 2>"$tmp/$name.err"
    rc=$?
    set -e
    [ "$rc" -ne 0 ] && cmp -s "$expected" "$tmp/$name.err" || {
        echo "audit-runtime-guards: $compiler $source returned $rc or wrong diagnostic";
        cat "$tmp/$name.err"; exit 1;
    }
}
cat >"$tmp/map_fast_cap.ty" <<'TY'
fn main():
    m := []int: int
    reserve(m, 9223372036854775807)
TY
cat >"$tmp/map_composite_cap.ty" <<'TY'
fn main():
    m := []string: string
    reserve(m, 9223372036854775807)
TY
cat >"$tmp/map_negative_cap.ty" <<'TY'
fn main():
    m := []int: int
    reserve(m, -1)
TY
cat >"$tmp/array_negative_cap.ty" <<'TY'
fn main():
    xs := []int
    reserve(xs, -1)
TY
printf 'tycho: map capacity 9223372036854775807 out of range\n' >"$tmp/map_cap.err"
printf 'tycho: map capacity -1 out of range\n' >"$tmp/map_negative.err"
printf 'tycho: reserve capacity -1 out of range\n' >"$tmp/reserve_negative.err"
mkdir "$tmp/regex_budget"
cat >"$tmp/regex_budget/main.ty" <<'TY'
package main
import "core:regex"

fn main():
    re := regex.compile("a+b")
    s := ""
    for i := 0; i < 4095; i += 1:
        s = s + "a"
    s = s + "!"
    println(str(regex.is_match(re, s)))
    regex.release(re)
TY
for compiler in ./tychoc ./tychoc1; do
    check_abort "$compiler" tests/abort/reserve_composite_overflow.ty tests/abort/reserve_composite_overflow.err reserve
    check_abort "$compiler" "$tmp/map_fast_cap.ty" "$tmp/map_cap.err" map_fast_cap
    check_abort "$compiler" "$tmp/map_composite_cap.ty" "$tmp/map_cap.err" map_composite_cap
    check_abort "$compiler" "$tmp/map_negative_cap.ty" "$tmp/map_negative.err" map_negative_cap
    check_abort "$compiler" "$tmp/array_negative_cap.ty" "$tmp/reserve_negative.err" array_negative_cap
    check_abort "$compiler" tests/abort/channel_capacity_overflow.ty tests/abort/channel_capacity_overflow.err channel
    check_abort "$compiler" tests/fixtures/audit_regex_subject_limit/main.ty tests/regex_subject_limit.err regex_limit
    "$compiler" tests/fixtures/audit_regex_group_large/main.ty -o "$tmp/regex_group" >"$tmp/regex_group.build" 2>&1 || {
        echo "audit-runtime-guards: $compiler failed to build regex group fixture"; cat "$tmp/regex_group.build"; exit 1;
    }
    [ "$("$tmp/regex_group")" = "-1" ] || {
        echo "audit-runtime-guards: $compiler accepted an out-of-range regex group"; exit 1;
    }
    "$compiler" "$tmp/regex_budget/main.ty" -o "$tmp/regex_budget-$(basename "$compiler")" >"$tmp/regex_budget.build" 2>&1 || {
        echo "audit-runtime-guards: $compiler failed to build the regex match-budget fixture"; cat "$tmp/regex_budget.build"; exit 1;
    }
    budget=$(timeout 10 "$tmp/regex_budget-$(basename "$compiler")") || {
        echo "audit-runtime-guards: $compiler exceeded the bounded regex match budget"; exit 1;
    }
    [ "$budget" = "false" ] || {
        echo "audit-runtime-guards: $compiler returned '$budget' for the failing regex budget case"; exit 1;
    }
done
echo "audit-runtime-guards: array/map capacities, channel, regex bounds and bounded match latency hold in both compilers"
