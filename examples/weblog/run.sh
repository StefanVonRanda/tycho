#!/bin/sh
# weblog -- build with tychoc, run the no-argument demo, and assert it matches the
# golden. Re-record: RECORD=1 sh examples/weblog/run.sh
# Not wired into `make ci`.
#
# Until 2026-07-29 this ran a SECOND leg: the self-hosted tychoc0 was built fresh
# from compiler/tychoc0.ty, transpiled main.ty to C, was linked against
# corelib/io/io_shim.c + corelib/datetime/datetime_shim.c by hand (tychoc
# auto-links a package's <pkg>_shim.c; the tychoc0 transpile path did not), and
# its output had to be byte-identical to tychoc's. That proved core:cli,
# core:io and core:datetime were compilable by the frozen compiler as well as the
# live one. tychoc0 is FROZEN and the breaking loop-syntax change of 2026-07-29
# means it can no longer parse the corpus, so no lane builds it -- see the header
# of compiler/fixpoint.sh, plus ROADMAP.md and docs/architecture.md. What is lost
# here specifically: this was one of only four runners that exercised the frozen
# compiler over real corelib packages.
set -u
cd "$(dirname "$0")/../.." || exit 2                  # repo root
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
CC="${CC:-cc}"
export TYCHO_CORELIB="$PWD/corelib"
D=examples/weblog
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT

# tychoc (C reference compiler)
$TYCHOC "$D/main.ty" -o "$T/wl_c" 2>"$T/err" || { echo "weblog: tychoc BUILD FAILED"; cat "$T/err"; exit 1; }
"$T/wl_c" > "$T/out_c"                                # no args -> embedded demo log

if [ "${RECORD:-0}" = "1" ]; then
    cp "$T/out_c" "$D/expected.out"; echo "weblog: golden recorded ($D/expected.out)"; exit 0
fi

fail=0
diff -u "$D/expected.out" "$T/out_c" || { echo "weblog: tychoc output differs from golden"; fail=1; }
[ $fail -eq 0 ] && echo "weblog: ok (tychoc == golden; the tychoc0 leg was retired 2026-07-29)" || exit 1
