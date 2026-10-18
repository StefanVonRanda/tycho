#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
CC=${CC:-cc}
t=$(mktemp -d)
trap 'rm -rf "$t"' EXIT HUP INT TERM
cp tests/fixtures/enum_recycle_size.src "$t/fixture.ty"
for tc in tychoc tychoc1; do
    ./$tc "$t/fixture.ty" --emit-c -o "$t/$tc" >/dev/null
    $CC -O2 -fwrapv -std=c11 -DTYCHO_ENUM_RECYCLE_GENERATED="\"$t/$tc.c\"" \
        tests/enum_recycle_size.c -lm -pthread -o "$t/test"
    "$t/test"
    # Reintroduce the old size mismatch only in this throwaway generated C.
    python3 - "$t/$tc.c" "$t/bad.c" <<'PY'
from pathlib import Path
import sys
src = Path(sys.argv[1]).read_text()
old = 'arena_recycle(a, v, offsetof(E_RecycleValue, u) + sizeof(TychoPayload_RecycleValue_Tiny));'
assert src.count(old) == 1, 'enum recycler control anchor drifted'
Path(sys.argv[2]).write_text(src.replace(old, 'arena_recycle(a, v, sizeof(*v));'))
PY
    $CC -O2 -fwrapv -std=c11 -DTYCHO_ENUM_RECYCLE_GENERATED="\"$t/bad.c\"" \
        tests/enum_recycle_size.c -lm -pthread -o "$t/bad"
    rc=0
    "$t/bad" >"$t/bad.out" 2>"$t/bad.err" || rc=$?
    [ "$rc" = 1 ]
    grep -q '^enum-recycle-size: payload reuse overwrote adjacent allocation$' "$t/bad.err"
done
echo 'enum-recycle-size: both emitters pass; both defeated controls fail as intended'
