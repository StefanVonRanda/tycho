#!/bin/sh
# webserver -- build with tychoc, run the deterministic self-test (routes
# dispatched through the pure handler, no socket), and assert it matches the
# golden. Re-record: RECORD=1 sh examples/webserver/run.sh
# For a live server:  ./server --serve   (optionally PORT=8137).
#
# IN `make ci` since 2026-08-02, inside step [3/13] beside site/raytrace/
# mandelbrot/fetch, as `make webserver`. Before that this runner already compared
# its golden and nothing ever ran it: `scripts/entrypoints.sh` proved only that
# main.ty compiles, and scripts/ci.sh said in as many words that the examples with
# their own runner "were outside this".
#
# WHAT THE GOLDEN ASSERTS, AND WHAT IT DELIBERATELY DOES NOT.
# The whole of stdout is compared -- NOTHING is excluded, and the no-argument
# self-test is shaped so that nothing needs to be. It dispatches each route
# through the PURE handler: no socket is created, so no port is bound and no port
# number can reach the output. `examples/webserver/main.ty@getenv` reads PORT only
# inside `if serve:`, the branch this runner never takes. The routes are a fixed
# list in the source, so the order is the source's and not a readdir's; the pages
# are rendered from `examples/webserver/main.ty@ROOT`, all of it tracked, so a
# change there is a real golden change and SHOULD redden here. No clock is read
# (unlike the access log, which is server-check's problem). The binary is built
# into a throwaway temp dir but that path never reaches stdout. Measured 2026-08-02:
# three runs of one build, `cmp` silent between all three and against the golden.
#
# THIS IS NOT server-check. That lane (`server/run.sh`, step [3c/13]) starts
# server/main.ty for real on `--port 0` and talks HTTP to it over a socket, and it
# excludes the bound port for exactly the reason this one has no port to exclude.
#
# Until 2026-07-29 this ran a SECOND leg: the self-hosted tychoc0 was built fresh
# from compiler/tychoc0.ty, transpiled main.ty to C, was linked against
# corelib/net/net_shim.c + corelib/io/io_shim.c by hand, and its output had to be
# byte-identical to tychoc's. That proved core:net and core:io were compilable by
# the frozen compiler as well as the live one. tychoc0 is FROZEN and the breaking
# loop-syntax change of 2026-07-29 means it can no longer parse the corpus, so no
# lane builds it -- see the header of compiler/fixpoint.sh, plus ROADMAP.md and
# docs/architecture.md. What is lost here specifically: this was one of only four
# runners that exercised the frozen compiler over real corelib packages.
set -u
cd "$(dirname "$0")/../.." || exit 2                  # repo root (so content paths resolve)
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
CC="${CC:-cc}"
export TYCHO_CORELIB="$PWD/corelib"
D=examples/webserver
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
# (SHIMS="corelib/net/net_shim.c corelib/io/io_shim.c" was needed only by the
#  retired tychoc0 leg; tychoc auto-links a package's <pkg>_shim.c itself.)

$TYCHOC "$D/main.ty" -o "$T/srv_c" 2>"$T/err" || { echo "webserver: tychoc BUILD FAILED"; cat "$T/err"; exit 1; }
"$T/srv_c" > "$T/out_c"                                # no args -> self-test

if [ "${RECORD:-0}" = "1" ]; then
    cp "$T/out_c" "$D/expected.out"; echo "webserver: golden recorded ($D/expected.out)"; exit 0
fi

fail=0
diff -u "$D/expected.out" "$T/out_c" || { echo "webserver: tychoc output differs from golden"; fail=1; }
[ $fail -eq 0 ] && echo "webserver: ok (tychoc == golden; the tychoc0 leg was retired 2026-07-29)" || exit 1
