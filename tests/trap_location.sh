#!/bin/sh
# -g builds tag runtime traps with the owning source line; TYCHO_BACKTRACE adds
# C frames. Normal builds print neither.
set -eu
cd "$(dirname "$0")/.."
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT HUP INT TERM

printf 'fn main():\n    xs := [1, 2, 3]\n    println(str(xs[5]))\n' >"$T/main.ty"
./tychoc "$T/main.ty" -g -o "$T/g" >/dev/null 2>&1
if "$T/g" >/dev/null 2>"$T/g.err"; then echo "trap-location: bounds check did not fire"; exit 1; fi
grep -q 'tycho: index 5 out of bounds (len 3)' "$T/g.err" || { echo "trap-location: wrong trap text"; cat "$T/g.err"; exit 1; }
grep -q "at $T/main.ty:3" "$T/g.err" || { echo "trap-location: missing source line"; cat "$T/g.err"; exit 1; }

TYCHO_BACKTRACE=1 "$T/g" >/dev/null 2>"$T/b.err" || true
grep -q 'backtrace (' "$T/b.err" || { echo "trap-location: TYCHO_BACKTRACE produced no frames"; cat "$T/b.err"; exit 1; }

./tychoc "$T/main.ty" -o "$T/n" >/dev/null 2>&1
if "$T/n" >/dev/null 2>"$T/n.err"; then echo "trap-location: normal build did not fire"; exit 1; fi
grep -q '^  at ' "$T/n.err" && { echo "trap-location: normal build printed a source line"; cat "$T/n.err"; exit 1; }

mkdir -p "$T/pkg/lib"
printf 'package main\nimport "./lib"\nfn main():\n    lib.bad()\n' >"$T/pkg/main.ty"
printf 'package lib\nfn bad():\n    xs := [1]\n    println(str(xs[9]))\n' >"$T/pkg/lib/lib.ty"
./tychoc "$T/pkg/main.ty" -g -o "$T/pkg/g" >/dev/null 2>&1
if "$T/pkg/g" >/dev/null 2>"$T/p.err"; then echo "trap-location: package trap did not fire"; exit 1; fi
grep -q "lib/lib.ty:4" "$T/p.err" || { echo "trap-location: package trap named the wrong file"; cat "$T/p.err"; exit 1; }

echo "trap-location: -g source line, package file, backtrace and normal-build silence all passed"
