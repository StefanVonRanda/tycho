#!/bin/sh
# Compact array descriptor probe: 24-byte {data,len,cap} vs 8-byte prefixed pointer.
# Prints median-of-5 peak RSS (MB) and wall time (ms) per workload and layout.
set -eu
cd "$(dirname "$0")"
CC="${CC:-cc}"; T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
$CC -O2 -o "$T/peakrss" ../peakrss.c
$CC -O2 -o "$T/wide" probe.c
$CC -O2 -DCOMPACT -o "$T/compact" probe.c
to_kb() { case "$(uname)" in Darwin) echo $(( $1 / 1024 ));; *) echo "$1";; esac; }
med() { sort -n | sed -n 3p; }
printf '%-8s %-8s %9s %7s   %s\n' workload layout peakMB ms checksum
for w in struct3 adj orders; do
  for l in wide compact; do
    : > "$T/rss"; : > "$T/ms"
    for i in 1 2 3 4 5; do
      "$T/peakrss" "$T/$l" "$w" > "$T/out_$l" 2> "$T/m"
      to_kb "$(awk '{print $1}' "$T/m")" >> "$T/rss"; awk '{print $2}' "$T/m" >> "$T/ms"
    done
    printf '%-8s %-8s %9s %7s   %s\n' "$w" "$l" "$(med < "$T/rss" | awk '{printf "%.1f", $1/1024}')" \
      "$(med < "$T/ms")" "$(cat "$T/out_$l")"
  done
  cmp -s "$T/out_wide" "$T/out_compact" || { echo "CHECKSUM MISMATCH on $w"; exit 1; }
done
