#!/bin/sh
# Local CI gate for tycho. NO GitHub Actions, no cloud -- this runs on your machine.
# It is the single source of truth for "is the tree green": build, golden +
# sanitizer tests, and a fuzz campaign. Exits nonzero when any lane fails, so it
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
LANE="${2:-main}"
[ "$#" -le 2 ] || { echo "ci.sh: usage: scripts/ci.sh [FUZZ_N]" >&2; exit 2; }
# Fail-closed: a non-numeric FUZZ_N must abort, not silently skip the fuzz.
case "$N" in
    *[!0-9]*|"") printf 'ci.sh: FUZZ_N must be a non-negative integer, got "%s"\n' "$N" >&2; exit 2 ;;
esac
case "$LANE" in
    main|platform|corelib|apps|rest|fuzz-main|fuzz-reject|fuzz-leak) ;;
    *) printf 'ci.sh: unknown internal lane "%s"\n' "$LANE" >&2; exit 2 ;;
esac
# Windows/MSYS2: the Linux-only lanes cannot run -- no gcc -m32 multilib
# (ilp32), no mingw ASan/UBSan runtime (asan-self, the fuzz differential), no
# LD_PRELOAD (locale-check self-skips). They skip loudly by name, per
# plan_windows.md phase 6/7 ("skip loudly with their reasons").
case "$(uname -s)" in
    *MSYS*|*MINGW*|*CYGWIN*) IS_WINDOWS=1 ;;
    *) IS_WINDOWS=0 ;;
esac
case "$(uname -s):$(uname -m)" in
    Linux:x86_64|Linux:i?86) ILP32_HOST=1 ;;
    *) ILP32_HOST=0 ;;
esac

bar() { printf '================================================================\n'; }
step() { printf '\n>>> %s\n' "$1"; }
run_lanes() {
    pids=""
    for child in "$@"; do
        sh "$0" "$N" "$child" &
        pids="$pids $!"
    done
    failed=0
    for pid in $pids; do
        if ! wait "$pid"; then failed=1; fi
    done
    return "$failed"
}

if [ "$LANE" = main ]; then
ci_started=$(date +%s)
bar
printf ' tycho local CI   (no GitHub Actions -- runs here, on this machine)\n'
printf ' fuzz seeds: %s\n' "$N"
bar

step "[1/13] build (make tychoc)"
make -s tychoc

# Sub-lane of 1, not a step of its own: the /13 denominator counts the numbered
# steps and 2b/2c/2d/3b..3f are the existing convention. It sits FIRST -- ahead
# of every lane that consumes a golden -- for two reasons. It needs no build
# product at all (it is `git ls-files` over a text scan of the runners, ~0.07s),
# and every lane below it compares against goldens that are on THIS disk, so an
# untracked one leaves all of them green while a fresh clone dies with
# `no golden -- run RECORD=1`. Reporting that after eighteen minutes of lanes
# that could not see it is the wrong order.
#
# `tools/tycho-ar/expected.out` shipped untracked once and was caught by hand;
# `.gitignore`'s `*.out` rule plus its per-directory un-ignore list is the
# structural trap, not that one lane's mistake. `make test` cannot redden for
# this -- it reads the golden off the working tree, which is exactly the copy
# that exists.
# Sub-second, and FIRST for that reason: `path:line` citations are load-bearing
# here and were policed by nothing automated until 2026-08-10 -- `make check-links`
# existed and no lane called it, so a red citation gate rode a push to `main`.
step "[1a/13] make check-links  (relative links + every path:line citation resolves)"
make -s check-links

step "[1b/13] make goldens-check  (every golden a run.sh names is tracked by git -- the fresh-clone check)"
make -s goldens-check

step "[2/13] make test  (golden output + ASan/UBSan/LeakSanitizer)"
make -s test

run_lanes platform corelib apps rest
if [ "$N" -gt 0 ]; then
    if [ "$IS_WINDOWS" = 1 ]; then
        step "[6/13] fuzz lanes skipped (Windows: the differential builds ASan binaries; mingw has no -lasan/-lubsan -- plan_windows.md phase 2)"
    else
        run_lanes fuzz-main fuzz-reject fuzz-leak
    fi
else
    step "[6/13] fuzz lanes skipped (N=0)"
fi

bar
printf ' CI GREEN -- tree is good (%ss)\n' "$(( $(date +%s) - ci_started ))"
bar
exit 0
fi

# `make test` above runs on this LP64 host, where long == int64 hides every width
# bug. This lane re-runs the SAME fixture suite under `gcc -m32` (ILP32: 32-bit
# long, 32-bit pointers), so anything that lowered Tycho `int` to a 32-bit C type
# truncates and reddens. tests/int64_width.ty is the in-glob fixture that makes it
# non-vacuous (every value there exceeds 2^31).
if [ "$LANE" = platform ]; then
step "[2b/13] make ilp32  (fixture suite rebuilt under -m32: Tycho int stays 64-bit off LP64)"
if [ "$ILP32_HOST" = 0 ]; then
    echo "SKIP ilp32 (host is not Linux x86; int64 width is asserted by tests/int64_width.ty in step 2)"
else
    make -s ilp32
fi

# Both lanes above sanitize/rebuild the C tychoc EMITS. Neither -- nor anything
# else in this file before 2026-07-25 -- ever built src/tychoc.c itself with
# -fsanitize, so the compiler's OWN memory safety was unmeasured by every gate.
# That is how the front-door plan's stack-buffer-overflow WRITE in parse_type_inner,
# reachable from a valid program, survived 16 phases and a full 1.0 freeze. This
# lane builds the compiler under ASan+UBSan and compiles the whole corpus with it.
# tests/generic_many_typaram_names.ty is the in-corpus fixture that makes it
# non-vacuous: restore the [256] bound Phase 37 removed and this lane reddens.
# The corpus deliberately still includes compiler/tychoc0.ty -- as INPUT, the
# largest single Tycho source in the tree (~16k lines); no tychoc0 binary is built
# or run. ~14s total, so it runs every time with no subsetting. See
# scripts/asan_self.sh.
step "[2c/13] make asan-self  (the COMPILER built with ASan+UBSan, compiling the whole corpus)"
if [ "$IS_WINDOWS" = 1 ]; then
    echo "SKIP asan-self (Windows: mingw ASan is experimental -- no -lasan/-lubsan; the compiler's own memory safety is the CI's job)"
else
    make -s asan-self
fi

# rtparity joined the sweep on 2026-07-30 (the loops-cleanup plan). It was created by
# batch 9 and sat in NO aggregate lane, so nothing ran it. It is a tests/ lane, not
# a corelib dogfood, hence 2d rather than a new number; and it costs ~1s (one
# --emit-c, no cc), so there is no argument for keeping it out. It earns its place
# immediately: phase 53's deletion of the step codegen removed the
# "tycho: range step is zero" trap text, and this lane was the ONLY gate in the
# tree that noticed -- make test, make conc and asan-self were all green over it.
step "[2d/13] make rtparity  (the emitted runtime surface -- env knobs, tycho: traps, arena-stats rows -- vs the oracle)"
make -s rtparity

# 2e, not a new number: it re-reads two of step 2's own fixtures, so it is a
# sub-lane of the tests/ suite exactly as 2b/2c/2d are, and the /13 denominator
# counts the numbered steps.
#
# It closes a hole three plans in a row opened and none closed. Each fixed a
# locale defect -- the lexer's strtod, codegen's snprintf, the runtime's %g --
# and until this step `grep -n 'LC_ALL\|LC_NUMERIC\|LD_PRELOAD' scripts/ci.sh
# Makefile` returned NOTHING, so all three could regress with every gate green.
# They are latent by nature: a C program starts in the "C" locale whatever
# LC_ALL says, until a linked library calls setlocale, and nothing here does.
# That also means the obvious spelling of this step -- an LC_ALL= prefix on the
# make line -- would be INERT and was measured so on a fully broken compiler.
# The step LD_PRELOADs a constructor that calls setlocale instead.
#
# It SKIPS (loudly, by name, exit 0) where it cannot run: no comma-decimal
# locale, no locale(1), no buildable preload, or a preload the loader ignores.
# That is deliberate -- a machine without a Danish locale must not redden CI --
# and the skip prints its reason so it can never be mistaken for a pass. ~1.5s.
step "[2e/13] make locale-check  (both locale fixtures compiled AND run under a comma-decimal LC_ALL forced by an LD_PRELOAD constructor)"
make -s locale-check
fi

if [ "$LANE" = corelib ]; then
step "[3/13] make corelib  (corelib packages + examples + the site/raytrace/mandelbrot/fetch/weblog/webserver dogfoods vs goldens)"
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
# weblog and webserver joined this step on 2026-08-02, and the paragraph below
# used to name them as the examples this file did not run. Both runners already
# compared their own expected.out with `diff -u`; what was missing was any lane
# that invoked them, so the two goldens were asserted by nothing -- the same shape
# as fetch's stale golden above, and un-ignoring them (see .gitignore) had made
# them visible without making them checked. Both are deterministic with NOTHING
# excluded from the comparison (weblog's log is embedded in its source; the
# webserver leg run here dispatches a fixed route list through the pure handler
# and binds no socket, so there is no port in the output). ~4s for the pair,
# measured 2026-08-02. They need cc and libc only -- no network, no sqlite3 -- so
# like the four above they are safe here unconditionally.
make -s weblog
make -s webserver
fi

if [ "$LANE" = apps ]; then
# Step 3 above builds corelib, corelib-examples, site, raytrace, mandelbrot,
# fetch, weblog and webserver -- and NOTHING else in the tree with an entry point.
# Four runners remain outside it: examples/sqlite (deliberately -- it needs
# sqlite3) and examples/life, examples/minesweeper, examples/snake, which each
# hold a golden that no lane compares -- the same hole this step just closed for
# weblog and webserver, filed rather than absorbed on 2026-08-02.
# That gap is why examples/webserver/main.ty once sat uncompilable for a whole
# phase with no gate red -- webserver is covered twice over now, here and by its
# own golden above, but those four reach this lane only. It is compile-only (`--emit-c`: no cc, no link, no libcurl/sqlite3)
# and costs milliseconds, so closing that hole is not a reason to run `make ci`
# less often. It does NOT assert freeze parity -- see scripts/entrypoints.sh.
step "[3b/13] make entrypoints  (every entry point in the tree still compiles)"
make -s entrypoints

# server-check joined the sweep on 2026-07-30 (the webserver-gate plan). server/main.ty is
# the largest program in the tree and until the webserver-gate plan NOTHING ran it: `make
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

# A sub-lane of 3 because its subject is the corelib shims, but note it is NOT a
# stricter rerun of step 3: the real build appends a shim to the generated .c on
# one cc line with no -std flag, so a shim missing a feature-test macro compiles
# there and `make corelib` stays green. The two lanes are independent -- this one
# can redden while every dogfood above passes, which is the whole reason it is
# here. Runs in well under a second; shims whose `deps` package is absent are
# skipped, same rule as corelib/run.sh:39.
step "[3d/13] make shim-check  (every corelib <pkg>_shim.c compiles standalone under -std=c11)"
make -s shim-check

# A sub-lane of 3 because it is a dogfood compared against a recorded golden,
# which is what every leg of step 3 is. Numbered 3e, not 14: 2b/2c/2d/3b/3c/3d
# are the existing convention for a sub-lane and the /13 denominator counts the
# numbered steps. It sits here rather than beside tools-check at [9] because its
# subject is a PROGRAM composing core:compress + sha256 + io + path + cli + sort
# + strings, not the tooling's own quality -- and because it costs ~2s where 9
# costs a minute.
#
# It closes a real hole rather than adding redundancy, and the hole is narrower
# than "nothing covered it": step [9] tools-check sweeps every .ty in the tree and
# runs `--emit-c` over each for its semantic-preservation leg, so tycho-ar failing
# to COMPILE already reddened there. What no lane did was RUN it -- [3b]
# entrypoints globs examples/*/ plus server/main.ty and never looks under tools/.
# So before this step the archiver could stop being deterministic, stop round
# tripping, or start extracting through `../`, and `make ci` stayed green.
step "[3e/13] make ar-check  (tycho-ar: create twice byte-identical, t vs golden, diff -r round trip, damage and path traversal refused)"
make -s ar-check

# 3f for the same reason 3e is 3e: a dogfood compared against a recorded golden,
# which is what every leg of step 3 is, and the /13 denominator counts the
# numbered steps. It sits beside ar-check because it closes the same hole for the
# same directory -- step [9] tools-check `--emit-c`s every .ty in the tree, so
# tycho-q failing to COMPILE already reddens there, and [3b] entrypoints never
# looks under tools/, so before this step nothing RAN it. An evaluator that
# started dropping rows, ordering "10" before "9", or truncating a decimal would
# have kept `make ci` green.
#
# Unlike the archiver's, this golden is also an assertion about FOUR corelib
# packages at once -- core:csv, core:json, core:decimal and core:sort -- because
# the transcript records what they actually return for a header, a `1.50`, a
# 26-digit integer and a multi-key order. ~3.5s.
step "[3f/13] make q-check  (tycho-q: 31-query transcript vs golden, select * byte-identical to the input, CSV == JSON, ten failure legs refused with empty stdout)"
make -s q-check

# 3g for the same reason 3e and 3f are: a dogfood compared against a recorded
# golden, and the /13 denominator counts the numbered steps. Third lane closing
# the same hole for the same directory -- [9] tools-check compiles every .ty
# under tools/, [3b] entrypoints never looks there, so nothing ran the VM.
#
# What this one adds that the other two cannot: an assembler and a disassembler
# that must agree byte for byte, and seven runtime traps that must each name
# their pc and leave stdout empty. A VM that started wrapping on overflow,
# jumping one past the end, or printing half a result before dying would have
# kept `make ci` green. ~2.3s.
step "[3g/13] make vm-check  (tycho-vm: asm deterministic, dis round-trips byte-identically, listings + fib/gcd/sort output vs golden, trace deterministic, 7 runtime traps + 4 malformed sources refused with empty stdout)"
make -s vm-check
step "[3h/14] make scheme-check  (tycho-scheme: fib/closures/ho/sort vs golden byte-identically on two runs; 5 error cases die non-zero with empty stdout)"
make -s scheme-check
step "[3i/15] make kv-check  (tycho-kv: 3 command scripts byte-identical B+ tree vs map backend; reloads reproduce; golden locked)"
make -s kv-check
step "[3j/16] make chess-check  (tycho-chess: perft totals vs published values on start/kiwipete/pos3 + ep/promo/castling vs oracle; search deterministic + TT-invariant with exact tactical probes)"
make -s chess-check
step "[3k/17] make rsa-check  (tycho-rsa: textbook vector, 3 python-pow modexp sizes, Miller-Rabin probes incl. Carmichael 561, deterministic keygen invariants + round-trips; 512-bit keygen round-trip)"
make -s rsa-check
step "[3l/18] make kvsrv-check  (tycho-kvsrv: HTTP KV round-trips + 405/404 + keep-alive + 4-way concurrent PUT/GET intact through the actor store)"
make -s kvsrv-check
step "[3m/19] make sat-check  (tycho-sat: PHP(2..9) all UNSAT; planted instances SAT with runner-verified models; deterministic; learning comparison recorded)"
make -s sat-check
step "[3n/20] make build-check  (tycho-build: first-build dispatch golden, second-build no-op differential, touch-rebuilds-only-dependents, failed recipe skips dependents, determinism, exit-2 errors)"
make -s build-check

# 3p for the same reason 3e-3o are: a tool under tools/ that nothing else runs --
# step [9] tools-check `--emit-c`s every .ty in the tree, so tycho-debug failing
# to COMPILE already reddens there, and [3b] entrypoints never looks under
# tools/. What no lane did was RUN it: before this step the gdb adapter could
# stop setting breakpoints, stop stepping, or hang on Ctrl-C with `make ci`
# green. It is a behavioral lane rather than a golden lane on purpose: the
# transcript includes gdb's own output, which drifts across gdb versions. It
# SKIPS loudly when gdb is absent (an external dependency like sqlite3/libpng),
# and it also exercises the `tycho debug` dispatcher command end to end.
step "[3o/21] make debug-check  (tycho-debug: scripted sessions -- breakpoint set + hit on the right source line, stripped-C-name locals, print, step, clean quit, run-to-completion with program output, Ctrl-C interrupt of a running inferior, fail-closed refusals, tycho debug wrapper)"
make -s debug-check

# 3p for the same reason 3e-3o are: a tool under tools/ that nothing else runs.
# tycho-db is the first program in the tree with its own internal packages, and
# a database has two ways to betray a caller -- return the wrong rows, or lose
# rows a process already acknowledged. The lane splits along that line: a golden
# carrying the ROWS plus a two-run determinism cmp of the transcript AND the
# store file, then a four-process persistence leg whose expected rows are
# literals in the runner rather than a slice of the golden. All eleven named
# error variants of store.StoreErr and exec.ExecErr must exit non-zero with
# their own whole message; the variant list is read out of the enums, so a new
# one cannot arrive ungated.
step "[3p/22] make db-check  (tycho-db: demo transcript + store file + log reproducible over two runs, rows survive a process exit and a reopened store takes writes, a real kill -9 mid-script replays idempotently and discards a torn record, the index and scan paths return identical rows while examining 1 against 6, a real server answers over TCP and survives rude clients, 25 error variants each refused with their own message)"
make -s db-check

# 3q for the same reason 3e-3p are: a tool under tools/ that nothing else runs.
# What makes it different from its neighbours is that its claims are about
# SCHEDULING, and a golden cannot see any of them -- a pipeline that lost its
# ordering, stopped being bounded, or races on the ring can all print the
# expected bytes on a kind run. So the lane varies the pool width and demands
# the same bytes, then proves the pool really does deliver out of order (a
# determinism claim over a pipeline that never raced is vacuous), then asserts
# the bounded ring against literals rather than the golden, then runs the whole
# thing under TSan -- because a capture bug here is a data race, which is right
# on this machine and wrong on the next.
step "[3q/23] make flow-check  (tycho-flow: transcript byte-identical over 8 runs and at TYCHO_THREADS=1 and 2, the pool proved to drain out of source order on 200 runs and not at all on one thread, a 4-slot ring that parks send 5, a cancelled pipeline that stops its source under 64 of 256 while the never-fails control runs to 256, 5 FlowErr variants and graph's cross-package collect each refused with their own message, TSan silent over the demo and 15 more pipelines)"
make -s flow-check

# 3r for the same reason 3e-3q are: a tool under tools/ that nothing else runs.
# What makes it different from its neighbours is that its subject is UTF-8, and
# a golden is structurally blind to a UTF-8 bug: a backspace that removes one
# BYTE of a 2-byte codepoint leaves a lone lead byte that renders as plausible
# text, and a golden re-recorded from that build diffs clean against it. So the
# byte and codepoint counts are asserted against literals in the runner, where
# RECORD=1 cannot bless them, and the undo journal is closed into a loop --
# undone to an empty buffer and redone back to a byte-identical dump -- which
# demo.ed does not do.
step "[3r/24] make ed-check  (tycho-ed: demo transcript byte-identical over 2 runs, a backspace over a 2-byte codepoint asserted at 13->11 bytes and 11->10 codepoints and a forward delete of a 3-byte one at 19->16 and 13->12 against literals, no dump reporting INVALID UTF-8, 6 edits undone to an empty buffer and redone to a byte-identical dump, 7 BufErr variants each refused with their own message)"
make -s ed-check

# 3s for the same reason 3e-3r are: a tool under tools/ that nothing else runs.
# Its subject is FLOAT TEXT, and a golden is blind to it the same way 3r's is
# blind to UTF-8: a renderer that drops a digit and a golden re-recorded from
# that build agree with each other. So the round trip is asserted in the runner,
# over a corpus the runner generates -- rendered, parsed back, compared as
# doubles -- with the four values that motivated cell/dtoa.ty asserted one by
# one, because a count of 98411 does not say which values were in it.
step "[3s/25] make sheet-check  (tycho-sheet: demo transcript byte-identical over 2 runs, 98411 generated floats rendered and read back bit-equal with 0.1+0.2, 2^53, DBL_MAX and the min subnormal each asserted separately and none falling back to #NUM!, str(0.1+0.2) round-trips, a cycle NAMED F1 -> F2 -> F3 -> F1 and a self-reference G1 -> G1, 10000- and 100000-deep chains exact and four depth limits past them failing closed by name, 13 of 14 CellErr/ParseErr variants each exiting non-zero with their own whole message and the 14th proved unconstructible)"
make -s sheet-check

# 3t for the same reason 3e-3s are: a tool under tools/ that nothing else runs.
# Its subject is SWAP-REMOVE, and a golden is blind to it the way 3r's is blind
# to UTF-8 and 3s's to float text: a despawn that moves the last entity down and
# forgets to re-point its slot leaves the POOL LENGTH RIGHT, so every count and
# every dense walk still reads correctly and exactly one id addresses somebody
# else. So the survivor set is generated in the runner and compared as a whole
# block, separately from the count, and the two redden independently.
step "[3t/26] make sim-check  (tycho-sim: demo, sweep and stale transcripts each byte-identical over 2 runs, 12 of 24 entities surviving a scripted despawn sweep with the count computed in the runner and every survivor resolved through its id to the hp it was spawned with, a despawned id refused as dead and the same id refused as stale once its slot is handed on with the generation moved, 3 SimErr variants each refused with their own whole message)"
make -s sim-check

# 3u for the same reason 3e-3t are: a tool under tools/ that nothing else runs.
# Its subject is a TOPOLOGICAL ORDER, and a golden is blind to it the way 3r's is
# blind to UTF-8 and 3s's to float text: drop an edge in the parser and the order
# is still an order -- same nodes, each once, entirely plausible -- and a golden
# re-recorded from that build agrees with it. So the order is checked against
# literals, against the edges the program itself printed, and against a second
# rulefile one edge apart from the first.
step "[3u/27] make make-check  (tycho-make: demo report byte-identical over 2 runs, 8 nodes ordered by DECLARATION order where alphabetical would disagree, every printed edge respected with each node listed exactly once, one edge removed from a 4-node chain moving the order, 3 cycles NAMED including a self-edge and one with innocent nodes stuck behind it, 8 MakeErr variants each exiting non-zero with their own whole message and an empty stdout; then the executor: a cold build runs all 4 rules with zeta.o and alpha.o really running before app in the recipes' own trace, a no-op rebuild runs ZERO, changing one input reruns exactly its 2 dependents, moving a file's mtime with its bytes intact reruns NOTHING and calls it touched, a chain node starting before the wide level beside it finishes -- the leg the wavefront this replaced cannot pass -- the log is byte-identical over 6 runs at TYCHO_THREADS 1/2/8, and 6 BuildErr variants are accounted for)"
make -s make-check

# tycho-snap: the newest tool lane, and the only one whose subject is an archive
# read back by SOMEBODY ELSE'S implementation -- our own CRC over our own bytes
# proves nothing about interoperability.
step "[3v/28] make snap-check  (tycho-snap: transcript AND archive bytes identical over 2 runs, the 4-member entry set and its sorted ORDER exact against literals with sub/ descended, .txt filtered and skipme/ never entered, python3 zipfile testzip() None and a member's sha256 out of the archive equal to the file on disk, an empty selection a 22-byte EOCD read as 0 entries, a missing manifest exit 1 naming the file and an unknown option exit 2 naming it)"
make -s snap-check

# tycho-tally: the SQLite ledger, and the only lane whose subject is a TEST
# FRAMEWORK -- its control breaks an assertion in a copy and requires the suite
# to notice.
step "[3w/29] make tally-check  (tycho-tally: a 15-check core:testing suite passing twice byte-identically AND proven able to fail -- a copy with one expected total changed exits 1 naming the check and counting 1 of 15; three processes write the ledger and a fourth reads it back against literals, SQL doing the sort and the SUM; a non-numeric amount exits 2 and books nothing)"
make -s tally-check

# tycho-agg: the only lane that checks a generic INSTANTIATION reached codegen,
# by grepping the emitted C for the mangled symbol. (It is not the only program
# declaring generics -- tools/tycho-flow and examples/generics_tour do too.)
step "[3x/30] make agg-check  (tycho-agg: report identical over 2 runs and equal to the golden; north 3 / south 2 / east 1 against literals with an empty-key row dropped by the generic filter; --min filters the display without moving distinct=3; the emitted C carries pipe__keep__, pipe__to__ and pipe__group_into__ instantiated at the program's own Row type; missing file, absent column and unknown option each exit non-zero naming the thing)"
make -s agg-check

# tycho-tmpl: the only program using `sink`, and the only lane asserting that a
# consume rule still REFUSES four shapes.
step "[3y/31] make tmpl-check  (tycho-tmpl: render identical over 2 runs and equal to the golden; substitution against literals with a repeated key; all four sink shapes -- accumulate in a loop, collect then consume, create-grow-consume, count before consuming -- still refused with a sink diagnostic; a missing key exits 1 naming the placeholder; an unknown option refused by name)"
make -s tmpl-check

# tycho-stat: the only consumer of variadics and `zero$(T)` outside tests/, and
# the lane that checks the ANSWERS rather than reproducing them.
step "[3z/32] make stat-check  (tycho-stat: run identical over 2 runs and equal to the golden; count/sum/min/max/mean over a 201-number corpus each equal to the runner's own arithmetic; both empty identities exactly 0 from zero\$(T); negatives survive parsing and min; a non-numeric field refused by name rather than read as its leading digits; an empty generic variadic naming no type still refused with a message naming the cure; an unknown option refused by name)"
make -s stat-check

# tycho-ledger: the only program using newtypes ACROSS a package boundary, and
# the lane whose load-bearing leg is five refusals no transcript can show.
step "[4a/33] make ledger-check  (tycho-ledger: run identical over 2 runs and equal to the golden; totals and the --rate doubling against literals; all five distinctness violations still refused, each naming an UNMANGLED type; keys() hands back wrapped keys that index the map with no unwrap; a non-numeric amount and an unknown option each refused by name)"
make -s ledger-check
fi

if [ "$LANE" = rest ]; then
step "[4/13] make conc  (spawn/parallel-for/channels: native + ASan + TSan vs goldens)"
make -s conc

step "[5/13] make ffi  (extern fn vs golden, ASan-clean, handle/injection bans)"
make -s ffi
fi

if [ "$LANE" = fuzz-main ]; then
    step "[6/13] make fuzz N=$N  (random programs: tychoc native -O2 vs tychoc ASan/UBSan)"
    python3 fuzz/run.py "$N"
fi
if [ "$LANE" = fuzz-reject ]; then
    step "[7/13] make fuzz-reject N=$N  (malformed input: tychoc must fail closed)"
    python3 fuzz/run_reject.py "$N"
fi
if [ "$LANE" = fuzz-leak ]; then
    # Leak bugs surface fast, so cap this sequential lane; make fuzz-leak runs deeper.
    LN="$N"; [ "$LN" -gt 150 ] && LN=150
    step "[8/13] make fuzz-leak N=$LN  (LeakSanitizer: arena / owner-0 leaks)"
    python3 fuzz/run_leak.py "$LN"
fi

if [ "$LANE" = rest ]; then
step "[9/13] make tools-check  (formatter idempotence + semantic preservation + LSP smoke)"
sh scripts/tools_check.sh

# Step 9 sweeps every .ty in the tree EXCEPT ./editors/*, which it excludes by
# name (scripts/tools_check.sh:30@editors), and no other step here mentions the directory
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
fi
