#!/bin/sh
# Dogfood: the `fetch` CLI composes core:http + json + sha256 + io + path. It is
# built by the C compiler and run against a local file:// fixture so the WHOLE
# pipeline (GET, parse, hash, cache) is exercised deterministically and offline
# (a real https GET is verified by hand). The emitted C is also run under
# ASan/UBSan to prove the curl response body's arena-copy is use-after-free-free.
# Skips if libcurl is absent.
# Re-record the golden with:  RECORD=1 sh examples/fetch/run.sh
#
# RETIRED LEG, 2026-07-29. Until then a second leg built the self-hosted tychoc0
# from compiler/tychoc0.ty, piped `tychoc --bundle` through it, linked the
# core:http shim + libcurl by hand, and required its output to be byte-identical
# to tychoc's. tychoc0 is FROZEN and the breaking loop-syntax change of 2026-07-29
# means it can no longer parse the corpus, so no lane builds it -- see the header
# of compiler/fixpoint.sh, plus ROADMAP.md and docs/architecture.md. What is lost:
# this was one of only four runners that fed the frozen compiler a real corelib
# program, and the only one that exercised its FFI/`--bundle` path over core:http.
#
# NOTE, because it is a coupling that could have been broken silently: the ASan
# leg (3) below used to sanitize the C that TYCHOC0 emitted, so removing leg (2)
# would have removed the sanitizer's input with it. It is repointed at tychoc's
# own `--emit-c` output instead. The use-after-free the lane exists to catch is
# in the shim and the arena copy, not in either compiler's codegen, so sanitizing
# tychoc's C covers it -- but it is now tychoc's codegen under the sanitizer, not
# a second implementation's.
set -u
cd "$(dirname "$0")/../.." || exit 2          # repo root
TYCHOC=./tychoc
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
export TYCHO_CORELIB="$PWD/corelib"
CC="${CC:-cc}"
# The SKIP guard and the sanitizer leg's link flags both used to name `libcurl`
# by hand. That is the THIRD form of the defect the shim list below had, and it
# is stale-prone for the same reason: nothing rechecks a hand-written dependency
# when main.ty's import graph moves, and the graph is what decides. ASK THE
# COMPILER: `--print-deps` prints the pkg-config names of the transitive closure,
# read out of the same `merge_pkg` walk `--print-shims` reads. It prints them
# whether or not they resolve on this host, which is exactly the case that
# matters -- where the library is missing the RESOLVED flags are empty and
# indistinguishable from "no dependencies", so only the names can say what to
# skip for.
DEPS="$("$TYCHOC" examples/fetch/main.ty --print-deps)" \
    || { echo "fetch: FAIL (tychoc --print-deps)"; exit 1; }
for pkg in $DEPS; do
    pkg-config --exists "$pkg" 2>/dev/null || { echo "fetch: SKIP ($pkg not installed)"; exit 0; }
done
DEPF=""
[ -n "$DEPS" ] && DEPF="$(pkg-config --cflags --libs $DEPS)"
# The --emit-c leg (2) links the shims by hand, so it has to know which ones.
# It ASKS THE COMPILER: `--print-shims` prints the transitive <pkg>_shim.c closure
# that the normal build path (leg 1) splices onto its own cc line. There is no
# hand-maintained list here any more, and the history below is why.
#
# THIS LIST WENT STALE TWICE, both times silently, both times the same shape.
# (1) Before the 2026-07-29 tychoc0 retirement, measured rather than assumed:
# running the pre-change run.sh at HEAD failed with `undefined reference to
# iox_close_lines / iox_stat_kind`, because the list named http_shim.c alone and
# core:io's shim was never added when main.ty gained its core:io imports.
# (2) On 2026-08-01, `core:strings` gained a shim (`strx_parse_double`, backing
# `strings.parse_float`) and the lane went red with `undefined reference to
# strx_parse_double` -- measured by running `make fetch` at d571b16 with
# everything else stashed, so it was this list being stale and nothing above it.
#
# NOTE WHAT WAS *NOT* THE FIX, because it is the trap the whole phase exists to
# avoid. examples/site/run.sh used to auto-discover its shims by grepping its own
# source for `core:` imports, and that would NOT have caught break (2):
# examples/fetch/main.ty does not import core:strings. It imports core:json, and
# corelib/json/json.ty imports core:strings -- the dependency is TRANSITIVE, and a
# grep over this program's own imports finds nothing to add. Measured on this tree
# after the flag landed: the grep yields http+io, `--print-shims` yields
# http+strings+io. Only a walk of the import graph sees it, and the compiler was
# already doing that walk (`merge_pkg` recurses, adding each package's shim as it
# enters); the flag just prints what it found.
SHIM="$("$TYCHOC" examples/fetch/main.ty --print-shims)" \
    || { echo "fetch: FAIL (tychoc --print-shims)"; exit 1; }
RECORD="${RECORD:-0}"
golden=examples/fetch/expected.out
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
# mingw gcc ships no sanitizer runtime -- see the SKIP at the sanitizer leg below
case "$(uname -s)" in *MSYS*|*MINGW*|*CYGWIN*) IS_WINDOWS=1 ;; *) IS_WINDOWS=0 ;; esac
# The fixture is fetched over file://, so the URL needs a path the NATIVE program
# can open. Under MSYS2 $PWD is the POSIX view (/c/tycho), which libcurl -- a
# native Windows DLL -- cannot resolve: the request came back "no response" and
# the lane read as a core:http failure. cygpath -m gives the mixed form
# (C:/tycho) that both a file: URL and the CRT accept.
if [ "$IS_WINDOWS" = 1 ]; then
    URL="file:///$(cygpath -m "$PWD")/examples/fetch/fixture.json"
else
    URL="file://$PWD/examples/fetch/fixture.json"
fi

# (1) C reference compiler (auto-discovers the core:http shim + deps)
if ! "$TYCHOC" examples/fetch/main.ty -o "$T/c" >"$T/c.log" 2>&1; then
    echo "FAIL: tychoc compile"; sed 's/^/      /' "$T/c.log"; fail=1
else
    "$T/c" "$URL" > "$T/c.out" 2>&1
fi

# (2) ASan/UBSan over tychoc's emitted C: the curl body pointer is transient, so
# it must be copied into the arena before the handle is released (else
# use-after-free on print/hash). --emit-c so we link the shim + libcurl ourselves
# under the sanitizer flags. (Before 2026-07-29 this sanitized tychoc0's C; see
# the header.)
if ! "$TYCHOC" examples/fetch/main.ty --emit-c -o "$T/san_src" >"$T/emit.log" 2>&1; then
    echo "FAIL: tychoc --emit-c"; sed 's/^/      /' "$T/emit.log"; fail=1
elif [ "$IS_WINDOWS" = 1 ]; then
    echo "SKIP fetch ASan/UBSan leg (mingw gcc ships no sanitizer runtime -- no -lasan/-lubsan; docs/internals/windows-port.md phase 2)"
elif ! $CC -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1 "$T/san_src.c" $SHIM -o "$T/san" -lm $DEPF 2>"$T/san.log"; then
    echo "FAIL: sanitizer cc"; sed 's/^/      /' "$T/san.log"; fail=1
else
    ASAN_OPTIONS=detect_leaks=0 "$T/san" "$URL" > "$T/san.out" 2>"$T/san.err" || { echo "FAIL: sanitizer fault"; sed 's/^/      /' "$T/san.err"; fail=1; }
    if grep -qiE 'runtime error|Sanitizer|ERROR: ' "$T/san.err"; then echo "FAIL: sanitizer report"; sed 's/^/      /' "$T/san.err"; fail=1; fi
fi

# THE GOLDEN IS BODY-DERIVED, NOT PATH-DERIVED. An earlier note here claimed the
# `tycho_fetch_<hash>.json` cache name hashed the URL, and that the URL embeds
# $PWD, so the golden could only reproduce in the directory it was recorded in.
# That was wrong, and it mattered: it is why the real cause went unfixed. The
# hash is `sha256.hex(body)` (examples/fetch/main.ty:45) sliced to 16 hex
# (examples/fetch/main.ty:65) -- the response BODY, never the URL -- and the only
# other URL-derived field is `path.base(url)` (examples/fetch/main.ty:46), a
# basename. Nothing in the output depends on $PWD; running the same binary
# against a copy of the fixture under an unrelated absolute path prints
# byte-identical output.
#
# The actual fault was a STALE GOLDEN. `39d75be` (Hier -> Tycho) rewrote
# fixture.json's body ("name": "hier" -> "tycho", 152 -> 153 bytes, hash
# e3de3da05e1cd879 -> 5124059f6a7ee320) and textually renamed the `hier_fetch_`
# prefix inside expected.out, but never re-recorded the `bytes`, `sha256` or the
# hash embedded in the cache name -- so the golden describes the pre-rename body.
# That commit's own message claims the dogfood digest goldens were re-recorded;
# this one was not, and no lane runs it, so nothing said so. Re-recorded 2026-07-30.
# That gap is closed: this lane joined `make ci` step [3/13] on 2026-07-30
# (`scripts/ci.sh` runs `make -s fetch`), so a stale golden here now reddens.
#
# RECORD is fail-closed: a golden is never written from a run that already
# failed, or the first red build would overwrite the assertion with its own
# broken output.
if [ "$RECORD" = 1 ]; then
    if [ "$fail" -ne 0 ]; then echo "FAIL: refusing to RECORD from a failed run"; exit 1; fi
    cp "$T/c.out" "$golden"; echo "rec  fetch"
fi
if [ "$fail" -eq 0 ] && [ ! -f "$golden" ]; then echo "FAIL: no golden -- run RECORD=1"; fail=1; fi
if [ "$fail" -eq 0 ] && ! cmp -s "$T/c.out" "$golden"; then
    echo "FAIL: output != golden"; diff "$golden" "$T/c.out" | sed 's/^/      /'; fail=1
fi
if [ "$IS_WINDOWS" = 1 ]; then SAN="ASan SKIPPED (no mingw runtime)"; else SAN="ASan"; fi
[ "$fail" -eq 0 ] && echo "fetch: green (http+json+sha256+io+path compose; tychoc+$SAN; real libcurl via file://; the tychoc0 leg was retired 2026-07-29)" || { echo "fetch: FAIL"; exit 1; }
