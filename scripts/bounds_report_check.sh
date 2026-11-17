#!/bin/sh
# Annotation check for `tychoc --bounds-report` (Goose's --bce-test).
#
# A fixture line carrying `# bounds: keep` must report `kept`, one carrying
# `# bounds: elide` must report `elided`, and a reported line with no
# annotation fails too -- so a new check appearing, or an elision silently
# lost, both turn this red. Only lines of the fixture file itself are judged.
#
# usage: scripts/bounds_report_check.sh [compiler] [fixture]
set -eu
tc=${1:-./tychoc1}
fx=${2:-tests/bounds_report/main.ty}
want=$(grep -nE '# bounds: (keep|elide)' "$fx" |
    sed -E 's/^([0-9]+):.*# bounds: (keep|elide).*/\1 \2/' |
    sed 's/ keep$/ kept/; s/ elide$/ elided/' | sort -u)
got=$("$tc" --bounds-report "$fx" | grep "^  $fx:" |
    sed "s|^  $fx:\([0-9]*\): \([a-z]*\).*|\1 \2|" | sort -u)
if [ "$want" != "$got" ]; then
    echo "bounds-report: $fx disagrees with its annotations"
    echo "--- want (line verdict)"; echo "$want"
    echo "--- got"; echo "$got"
    exit 1
fi
echo "bounds-report: $fx OK ($(echo "$want" | wc -l | tr -d ' ') annotated sites)"
