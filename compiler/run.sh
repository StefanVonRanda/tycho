#!/bin/sh
# Bootstrap test harness for tychoc0 — the subset compiler written in Tycho
# (Stage 1 of docs/bootstrap.md). Validates tychoc0 DIFFERENTIALLY against the C
# compiler: for each fixture P, cc(tychoc0(P)) and the C tychoc's binary must
# print identically. tychoc0 reads source on stdin (or a path arg) -> C on stdout.
set -u
cd "$(dirname "$0")/.."
[ -x ./tychoc ] || { echo "run 'make' first"; exit 2; }
CC="${CC:-cc}"
./tychoc compiler/tychoc0.ty -o /tmp/tychoc0 >/dev/null 2>&1 || { echo "tychoc0 failed to compile"; exit 1; }
fail=0
for f in compiler/tests/*.ty; do
    name="$(basename "$f")"
    ./tychoc "$f" -o /tmp/bs_ref >/dev/null 2>&1 || { echo "FAIL $name (C tychoc rejected it)"; fail=1; continue; }
    ref="$(/tmp/bs_ref)"
    if ! /tmp/tychoc0 < "$f" > /tmp/bs.c 2>/tmp/bs.err; then
        echo "FAIL $name (tychoc0 errored: $(cat /tmp/bs.err))"; fail=1; continue
    fi
    if ! $CC -O2 -o /tmp/bs_got /tmp/bs.c 2>/dev/null; then
        echo "FAIL $name (emitted C did not compile)"; fail=1; continue
    fi
    if [ "$ref" = "$(/tmp/bs_got)" ]; then echo "ok   $name"; else echo "FAIL $name"; fail=1; fi
done
[ "$fail" -eq 0 ] && echo "bootstrap: all green (tychoc0 matches the C compiler)" || { echo "bootstrap: FAIL"; exit 1; }
