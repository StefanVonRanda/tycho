#!/bin/sh
# Does a QUALIFIED builtin -- `strings.len(...)` -- name the builtin and the cure,
# in BOTH compilers, for EVERY builtin the surface freeze records?
#
# WHY THIS EXISTS. src/tychoc.c decided this with sig_find(), which has no entry
# for a generic builtin, so `strings.len(...)` died as "package 'strings' has no
# symbol 'len'" -- pointing the reader at a symbol that was never going to be
# there. tychoc1 tested its whole builtin table and said the useful thing. The two
# disagreed for a day and nothing noticed, because the gate that scores them is
# `make parse-check` and nothing runs it (FRICTION 106, 107).
#
# The fix put a FOURTH builtin list in src/tychoc.c, and a list that nothing
# checks is the same defect waiting. This reads the builtin set out of
# surface.lock -- the frozen one -- and requires both compilers to name every one
# of them, so a builtin added without being listed reddens here.
#
# Leg [C] is the control: a name that is NOT a builtin must still get the
# "has no symbol" message, or this script would pass by answering every input the
# same way.
set -eu
cd "$(dirname "$0")/.."
TYCHOC="${TYCHOC:-./tychoc}"
TYCHOC1="${TYCHOC1:-./tychoc1}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT INT TERM

probe() {   # $1 = name -> writes a program qualifying it through core:strings
    printf 'package main\nimport "core:strings"\nfn main():\n    println(str(strings.%s("hi")))\n' "$1" > "$T/main.ty"
}
msg_ref() { "$TYCHOC" "$T/main.ty" -o "$T/out" 2>&1 | grep -oE 'error:.*' | head -1; }
msg_sh()  { "$TYCHOC1" "$T/main.ty" --typecheck 2>&1 | grep -oE 'error:.*' | head -1; }

names="$(python3 -c "import json;print(' '.join(sorted(json.load(open('surface.lock'))['builtins'])))")"
n=0; bad=0
for b in $names; do
    n=$((n + 1))
    probe "$b"
    a="$(msg_ref)"; c="$(msg_sh)"
    case "$a" in
        *"is a builtin, not a member of package"*) ;;
        *) echo "  FAIL $b: tychoc did not name it a builtin"; echo "    got: $a"; bad=$((bad + 1)); continue ;;
    esac
    [ "$a" = "$c" ] || { echo "  FAIL $b: the two compilers disagree"; echo "    tychoc : $a"; echo "    tychoc1: $c"; bad=$((bad + 1)); }
done

probe "definitely_not_a_builtin"
ctl_a="$(msg_ref)"; ctl_c="$(msg_sh)"
case "$ctl_a" in
    *"has no symbol"*) echo "  [C] control: a non-builtin still gets 'has no symbol'" ;;
    *) echo "  [C] FAIL control: a non-builtin did NOT get 'has no symbol' -- this script answers everything the same way"; echo "    got: $ctl_a"; bad=$((bad + 1)) ;;
esac
[ "$ctl_a" = "$ctl_c" ] || { echo "  [C] FAIL control: the two compilers disagree on the non-builtin"; echo "    tychoc : $ctl_a"; echo "    tychoc1: $ctl_c"; bad=$((bad + 1)); }

[ "$bad" -eq 0 ] || { echo "builtin-qualified: FAILED ($bad)"; exit 1; }
echo "builtin-qualified: ok ($n builtins from surface.lock, both compilers name each one and agree word for word; a non-builtin still gets 'has no symbol')"
