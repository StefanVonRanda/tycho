#!/bin/sh
# Exercise allocator ownership separately from generated-language semantics.
set -eu
cd "$(dirname "$0")/.."
CC=${CC:-cc}
t=$(mktemp -d)
trap 'rm -rf "$t"' EXIT HUP INT TERM

build() {
    rt=$1 out=$2; shift 2
    $CC "$@" -fwrapv -ffp-contract=off -I "$rt" tests/allocator_contract.c -lm -pthread -o "$out"
}
run_modes() {
    for stats in 0 full; do
        TYCHO_ARENA_STATS=$stats "$1" >"$t/out" 2>"$t/err" || {
            cat "$t/err" >&2; exit 1;
        }
        grep -q '^allocator-contract: ownership, recycling, survivors, reset and append state hold$' "$t/out"
    done
}
build runtime "$t/native" -O2
run_modes "$t/native"
case "$(uname -s)" in
    *MSYS*|*MINGW*|*CYGWIN*) noasan=1; reason='mingw sanitizer runtime unavailable' ;;
    *) noasan=${TYCHO_NO_ASAN:-0}; reason='TYCHO_NO_ASAN requested' ;;
esac
if [ "$noasan" = 0 ]; then
    case "$(uname -s)" in Darwin) lsan=0 ;; *) lsan=1 ;; esac
    export ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}detect_leaks=$lsan"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:+$UBSAN_OPTIONS:}halt_on_error=1"
    build runtime "$t/sanitized" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
    run_modes "$t/sanitized"
else
    echo "allocator-contract: sanitizer leg SKIPPED ($reason)"
fi

# Defeat production code only in copies. Both patches must apply exactly once,
# compile, and fail for the invariant they defeat, not for an unrelated crash.
python3 - "$t" <<'PY'
from pathlib import Path
import sys
src = Path('runtime/tycho_rt.c').read_text()
patches = {
    'foreign': ('static int arena_owns(Arena *a, const void *p) {',
                'static int arena_owns(Arena *a, const void *p) {\n    return p != NULL;'),
    'reset': ('b->off = 0;                     /* keep & reuse the head block */',
              'b->off = b->cap;               /* defeated rewind */'),
}
for name, (old, new) in patches.items():
    assert src.count(old) == 1, (name, 'mutation anchor drifted')
    d = Path(sys.argv[1]) / name
    d.mkdir()
    (d / 'tycho_rt.c').write_text(src.replace(old, new))
PY
for bad in foreign reset; do
    build "$t/$bad" "$t/bad-$bad" -O2
    rc=0
    TYCHO_ARENA_STATS=0 "$t/bad-$bad" >"$t/out" 2>"$t/err" || rc=$?
    [ "$rc" = 1 ] || { echo "allocator-contract: $bad control returned $rc, expected 1" >&2; exit 1; }
    case "$bad" in
        foreign) why='child claims parent allocation' ;;
        reset) why='reset did not rewind head and release overflow' ;;
    esac
    grep -qF "allocator-contract: $why" "$t/err"
    echo "allocator-contract: defeated $bad invariant correctly rejected"
done
echo 'allocator-contract: native/stats modes and available sanitizers pass; both controls fail as intended'
