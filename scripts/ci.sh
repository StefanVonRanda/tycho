#!/bin/sh
# Local CI gate for tycho. NO GitHub Actions, no cloud -- this runs on your machine.
# It is the single source of truth for "is the tree green": build, golden +
# sanitizer tests, and a fuzz campaign. Exits nonzero on the FIRST failure so it
# composes into hooks and `make ci`.
#
# ONE COMPILER. Until 2026-07-26 thirteen of these steps asserted that tychoc and
# the self-hosted tychoc0 AGREE -- accept/reject parity, byte-identical output,
# the self-host fixpoint, the two runtimes' env knobs. tychoc0 is FROZEN (see the
# header of compiler/tychoc0.ty): it proved self-hosting and is now unmaintained,
# so no step here builds or runs it. Every surviving lane gates tychoc against a
# RECORDED GOLDEN or a stated invariant, never against a second implementation.
#
# Usage:
#   scripts/ci.sh [FUZZ_N]     FUZZ_N = fuzz seeds (default 200; 0 skips the fuzz)
#   make ci                    same, N defaults to 200 (override: make ci N=500 for a deeper sweep)
set -eu
cd "$(dirname "$0")/.."
N="${1:-200}"
# Fail-closed: a non-numeric FUZZ_N must abort, not silently skip the fuzz.
case "$N" in
    *[!0-9]*|"") printf 'ci.sh: FUZZ_N must be a non-negative integer, got "%s"\n' "$N" >&2; exit 2 ;;
esac

bar() { printf '================================================================\n'; }
step() { printf '\n>>> %s\n' "$1"; }

bar
printf ' tycho local CI   (no GitHub Actions -- runs here, on this machine)\n'
printf ' fuzz seeds: %s\n' "$N"
bar

step "[1/13] build (make tychoc)"
make -s tychoc

step "[2/13] make test  (golden output + ASan/UBSan/LeakSanitizer)"
make -s test

# `make test` above runs on this LP64 host, where long == int64 hides every width
# bug. This lane re-runs the SAME fixture suite under `gcc -m32` (ILP32: 32-bit
# long, 32-bit pointers), so anything that lowered Tycho `int` to a 32-bit C type
# truncates and reddens. tests/int64_width.ty is the in-glob fixture that makes it
# non-vacuous (every value there exceeds 2^31).
step "[2b/13] make ilp32  (fixture suite rebuilt under -m32: Tycho int stays 64-bit off LP64)"
make -s ilp32

# Both lanes above sanitize/rebuild the C tychoc EMITS. Neither -- nor anything
# else in this file before 2026-07-25 -- ever built src/tychoc.c itself with
# -fsanitize, so the compiler's OWN memory safety was unmeasured by every gate.
# That is how plan.md Phase 37's stack-buffer-overflow WRITE in parse_type_inner,
# reachable from a valid program, survived 16 phases and a full 1.0 freeze. This
# lane builds the compiler under ASan+UBSan and compiles the whole corpus with it.
# tests/generic_many_typaram_names.ty is the in-corpus fixture that makes it
# non-vacuous: restore the [256] bound Phase 37 removed and this lane reddens.
# The corpus deliberately still includes compiler/tychoc0.ty -- as INPUT, the
# largest single Tycho source in the tree (~16k lines); no tychoc0 binary is built
# or run. ~14s total, so it runs every time with no subsetting. See
# scripts/asan_self.sh.
step "[2c/13] make asan-self  (the COMPILER built with ASan+UBSan, compiling the whole corpus)"
make -s asan-self

# rtparity joined the sweep on 2026-07-30 (plan.md phase 58). It was created by
# batch 9 and sat in NO aggregate lane, so nothing ran it. It is a tests/ lane, not
# a corelib dogfood, hence 2d rather than a new number; and it costs ~1s (one
# --emit-c, no cc), so there is no argument for keeping it out. It earns its place
# immediately: phase 53's deletion of the step codegen removed the
# "tycho: range step is zero" trap text, and this lane was the ONLY gate in the
# tree that noticed -- make test, make conc and asan-self were all green over it.
step "[2d/13] make rtparity  (the emitted runtime surface -- env knobs, tycho: traps, arena-stats rows -- vs the oracle)"
make -s rtparity

step "[3/13] make corelib  (corelib packages + examples + the site/raytrace/mandelbrot/fetch dogfoods vs goldens)"
make -s corelib
make -s corelib-examples
make -s site
make -s raytrace
make -s mandelbrot
# fetch joined this step on 2026-07-30. It is the one dogfood composing core:http
# end to end, it needs no network (it GETs a file:// URL through libcurl) and it
# self-skips with `fetch: SKIP (libcurl not installed)`, so it is safe here
# unconditionally. Before this it was in no aggregate lane at all: its golden was
# left stale by 39d75be and stayed red, unnoticed, until plan.md batch 5.
make -s fetch

# Step 3 above builds corelib, corelib-examples, site, raytrace, mandelbrot and
# fetch -- and NOTHING else in the tree with an entry point. The remaining
# examples with their own runner (webserver, weblog, sqlite) were outside this
# file, so
# examples/webserver/main.ty once sat uncompilable for a whole phase with no gate
# red. This lane is compile-only (`--emit-c`: no cc, no link, no libcurl/sqlite3)
# and costs milliseconds, so closing that hole is not a reason to run `make ci`
# less often. It does NOT assert freeze parity -- see scripts/entrypoints.sh.
step "[3b/13] make entrypoints  (every entry point in the tree still compiles)"
make -s entrypoints

# server-check joined the sweep on 2026-07-30 (plan.md phase 2). server/main.ty is
# the largest program in the tree and until plan.md phase 1 NOTHING ran it: `make
# server` builds it and asserts nothing. It sits here, immediately after 3b,
# because 3b compiles server/main.ty for milliseconds -- so a server that does not
# build reddens there, with a compile error, instead of surfacing here as
# `FAIL readiness: no startup banner on stderr within 10s`; and it is placed ahead
# of the minute-scale fuzz/tools lanes because at ~4s it is cheap and it is the
# only lane covering core:net's accept/recv/send path end to end. Numbered 3c, not
# 14: 2b/2c/2d/3b are the existing convention for a sub-lane, and the /13
# denominator counts the numbered steps. It is a runner, not a compile check, so
# it is a sub-lane of 3b rather than a sixth dogfood inside step 3 -- everything in
# step 3 compares stdout to a recorded golden, and this one talks HTTP to a live
# daemon and asserts the answers. It self-skips with a SKIP line if python3 is
# absent (server/run.sh), and kills the daemon on every exit path including
# failure, so it cannot leave a bound port behind in the middle of a sweep.
step "[3c/13] make server-check  (tycho-httpd started for real: status codes, binary bodies, traversal, keep-alive, abuse suite, access log, SIGTERM)"
make -s server-check

step "[4/13] make conc  (spawn/parallel-for/channels: native + ASan + TSan vs goldens)"
make -s conc

step "[5/13] make ffi  (extern fn vs golden, ASan-clean, handle/injection bans)"
make -s ffi

if [ "$N" -gt 0 ]; then
    step "[6/13] make fuzz N=$N  (random programs: tychoc native -O2 vs tychoc ASan/UBSan)"
    python3 fuzz/run.py "$N"
    step "[7/13] make fuzz-reject N=$N  (malformed input: tychoc must fail closed)"
    python3 fuzz/run_reject.py "$N"
    # leak lane is the slowest (sequential ASan+LeakSanitizer) and leak bugs
    # surface fast (seeds <50), so cap it to keep `make ci` practical;
    # `make fuzz-leak N=...` runs a deeper sweep.
    LN="$N"; [ "$LN" -gt 150 ] && LN=150
    step "[8/13] make fuzz-leak N=$LN  (LeakSanitizer: arena / owner-0 leaks)"
    python3 fuzz/run_leak.py "$LN"
else
    step "[6/13] fuzz lanes skipped (N=0)"
fi

step "[9/13] make tools-check  (formatter idempotence + semantic preservation + LSP smoke)"
sh scripts/tools_check.sh

# Step 9 sweeps every .ty in the tree EXCEPT ./editors/*, which it excludes by
# name (scripts/tools_check.sh:25@editors), and no other step here mentions the directory
# -- so the two editor grammars were the one shipped artifact no gate ever
# parsed. editors/zed/grammars/tycho/src/parser.c is GENERATED from grammar.js;
# this lane regenerates it into a temp dir and cmp's, then parses the whole
# corpus with the result. Numbered 9b, not 14: 2b/2c/3b are the existing
# convention for a sub-lane of a step, and the /13 denominator counts the
# numbered steps. The tree-sitter CLI comes from npx, so the grammar lanes SKIP
# when it is unavailable; the JSON lane needs only python3 and always runs.
step "[9b/13] make editors-check  (zed grammar: src/ still generated from grammar.js, corpus still parses; vscode JSON is JSON)"
make -s editors-check

step "[10/13] bench-guard  (tree-alloc wall: tycho must beat C -- perf regression gate)"
sh bench/guard.sh

step "[11/13] make recursion  (deep input fails closed -- no stack-overflow DoS)"
make -s recursion

step "[12/13] make spec-check  (spec: Appendix A grammar == §3/§4 · Appendix E fixtures exist · runnable examples match their documented output)"
make -s spec-check

# NOTE: no backticks in this string. A literal ``` inside a double-quoted word
# is backquote command substitution to /bin/sh -- dash died here with
# `scripts/ci.sh: 133: Syntax error: EOF in backquote substitution` the first
# time this step was reached, which was batch 6's closing sweep. The step was
# added by batch 3 and never run inside a full `make ci` until then.
step "[12b/13] make docs-fences  (every fenced tycho block in docs/ that claims to be a whole program still compiles)"
make -s docs-fences

step "[13/13] make check-links  (every relative Markdown link resolves to a real file; every provenance citation still resolves)"
make -s check-links

bar
printf ' CI GREEN -- tree is good\n'
bar
