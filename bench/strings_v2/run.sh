#!/bin/sh
# Phase 4.1 probe: build each layout, check outputs agree, median of 5 RSS/time.
# Usage: bench/strings_v2/run.sh   (from the repo root)
set -eu
d=$(cd "$(dirname "$0")" && pwd)
o=${TMPDIR:-/tmp}/strings_v2.$$
mkdir -p "$o"; trap 'rm -rf "$o"' EXIT
cc -O2 -o "$o/peakrss" "$d/../peakrss.c"
for v in 0:0:today 1:0:ptrlen 2:0:sso 1:1:ptrlen+view 2:1:sso+view; do
    IFS=: read -r l w name <<EOF
$v
EOF
    cc -O2 -DLAYOUT="$l" -DVIEWS="$w" -o "$o/$name" "$d/probe.c"
done
# ru_maxrss is bytes on macOS, KiB on Linux
div=1024; [ "$(uname)" = Darwin ] && div=1048576
row() {  # workload name args...
    w=$1; name=$2; shift 2
    : > "$o/m"
    for _ in 1 2 3 4 5; do
        "$o/peakrss" "$o/$name" "$@" > "$o/out.$name" 2>> "$o/m"
    done
    if [ -f "$o/ref.$w" ]; then
        cmp -s "$o/ref.$w" "$o/out.$name" || { echo "MISMATCH $w $name"; exit 1; }
    else
        cp "$o/out.$name" "$o/ref.$w"
    fi
    rss=$(cut -d' ' -f1 "$o/m" | sort -n | sed -n 3p)    # medians, per column
    ms=$(cut -d' ' -f2 "$o/m" | sort -n | sed -n 3p)
    awk -v w="$w" -v n="$name" -v r="$rss" -v t="$ms" -v d="$div" \
        'BEGIN { printf "| %-6s | %-12s | %8.1f | %6d |\n", w, n, r / d, t }'
}
echo "| work   | layout       | RSS MiB  | ms     |"
echo "|--------|--------------|----------|--------|"
for n in today ptrlen sso; do row orders "$n" orders 1000000; done
for n in today ptrlen sso ptrlen+view sso+view; do row split "$n" split 200000 5; done
echo; cat "$o/ref.orders" "$o/ref.split"
