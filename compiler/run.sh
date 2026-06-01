#!/bin/sh
# Bootstrap test harness for hierc0 — the subset compiler written in Hier
# (Stage 1 of docs/bootstrap.md). Validates hierc0 DIFFERENTIALLY against the C
# compiler: for each fixture P, cc(hierc0(P)) and the C hierc's binary must
# print identically. hierc0 reads source on stdin and writes C to stdout.
set -u
cd "$(dirname "$0")/.."
[ -x ./hierc ] || { echo "run 'make' first"; exit 2; }
CC="${CC:-cc}"
./hierc compiler/hierc0.hi -o /tmp/hierc0 >/dev/null 2>&1 || { echo "hierc0 failed to compile"; exit 1; }
fail=0
for f in compiler/tests/*.hi; do
    name="$(basename "$f")"
    ./hierc "$f" -o /tmp/bs_ref >/dev/null 2>&1 || { echo "FAIL $name (C hierc rejected it)"; fail=1; continue; }
    ref="$(/tmp/bs_ref)"
    if ! /tmp/hierc0 < "$f" > /tmp/bs.c 2>/tmp/bs.err; then
        echo "FAIL $name (hierc0 errored: $(cat /tmp/bs.err))"; fail=1; continue
    fi
    if ! $CC -O2 -o /tmp/bs_got /tmp/bs.c 2>/dev/null; then
        echo "FAIL $name (emitted C did not compile)"; fail=1; continue
    fi
    if [ "$ref" = "$(/tmp/bs_got)" ]; then echo "ok   $name"; else echo "FAIL $name"; fail=1; fi
done
[ "$fail" -eq 0 ] && echo "bootstrap: all green (hierc0 matches the C compiler)" || { echo "bootstrap: FAIL"; exit 1; }
