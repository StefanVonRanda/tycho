#!/bin/sh
# Real CLI comparison: tree hasher, Tycho vs a Go stdlib peer.
set -eu
cd "$(dirname "$0")/../.." || exit 2
TYCHOC="${TYCHOC:-./tychoc1}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
"$TYCHOC" tools/tycho-hash/main.ty -o "$T/tycho-hash" >/dev/null
go build -o "$T/go-hash" bench/cli/hash.go
mkdir -p "$T/tree"
python3 - "$T/tree" <<'PY'
from pathlib import Path
import sys
r=Path(sys.argv[1])
for i in range(512):
    (r/f"f{i:03}.bin").write_bytes(bytes([i%251])*(256*1024))
PY
"$T/tycho-hash" "$T/tree" --workers=8 > "$T/tycho.out"
"$T/go-hash" "$T/tree" 8 > "$T/go.out"
cmp -s "$T/tycho.out" "$T/go.out" || { echo "cli hash: OUTPUT MISMATCH"; exit 1; }
run() {
    name=$1; bin=$2; arg=$3
    bestt=999999; bestr=0; i=0
    while [ $i -lt 3 ]; do
        /usr/bin/time -f '%e %M' -o "$T/time" "$bin" "$T/tree" "$arg" >/dev/null
        set -- $(cat "$T/time")
        ok=$(awk -v a="$1" -v b="$bestt" 'BEGIN{print (a<b)?1:0}')
        if [ "$ok" = 1 ]; then bestt=$1; bestr=$2; fi
        i=$((i+1))
    done
    printf '%-6s %ss %sKB\n' "$name" "$bestt" "$bestr"
}
run tycho "$T/tycho-hash" "--workers=8"
run go "$T/go-hash" "8"
