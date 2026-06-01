#!/bin/sh
# Bootstrap test harness for hierc0 — the subset compiler written in Hier
# (Stage 1 of docs/bootstrap.md). Builds hierc0 with the C compiler, then
# validates it on each fixture in compiler/tests/.
#
# Stage 1A (current): token-dump validation — each compiler/tests/<name>.hi is
# lexed and compared to <name>.tokens. This switches to the real oracle
# `cc(hierc0(P)) | run == golden` once codegen lands.
set -u
cd "$(dirname "$0")/.."
[ -x ./hierc ] || { echo "run 'make' first"; exit 2; }
./hierc compiler/hierc0.hi -o /tmp/hierc0 >/dev/null 2>&1 || { echo "hierc0 failed to compile"; exit 1; }
fail=0
for f in compiler/tests/*.hi; do
    base="${f%.hi}"
    if [ "$(/tmp/hierc0 < "$f")" = "$(cat "$base.tokens")" ]; then
        echo "ok   $(basename "$f")"
    else
        echo "FAIL $(basename "$f")"; fail=1
    fi
done
[ "$fail" -eq 0 ] && echo "bootstrap: all green" || { echo "bootstrap: FAIL"; exit 1; }
