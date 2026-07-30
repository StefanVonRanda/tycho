#!/bin/sh
# asan-self — gate the COMPILER'S OWN memory safety.
#
# WHAT WAS MISSING, AND WHY THIS EXISTS
# -------------------------------------
# Every other sanitizer lane in this repo sanitizes what tychoc *produces*, never
# tychoc *itself*:
#   tests/run.sh:74-75   builds the EMITTED C of each fixture with
#                        -fsanitize=address,undefined -fno-sanitize-recover=all
#   Makefile:103-106     documents that lane
#   Makefile:270@SKIPPED,:246  disable it for -m32 (32-bit ASan is absent under multilib)
# Nothing anywhere builds src/tychoc.c with -fsanitize. So the reference
# compiler's own memory safety was unmeasured by every gate, in either direction.
#
# That gap is how a stack-buffer-overflow WRITE in the compiler — 4 bytes past a
# 1024-byte frame object in parse_type_inner, reachable from a VALID program —
# survived 16 phases of plan.md and a full 1.0 freeze (plan.md Phase 37). It was
# found by hand, not by a gate.
#
# WHAT THIS LANE DOES
# -------------------
# Build src/tychoc.c with ASan+UBSan, then COMPILE every fixture in the corpus
# with that binary. The subject is the compiler's execution, so `--emit-c` is
# enough: the emitted programs are never built or run here — tests/run.sh already
# sanitizes those, and duplicating it would double the cost for no new coverage.
#
# The verdict is NOT the compiler's accept/reject decision. tests/reject/ and
# tests/diag/ fixtures are *supposed* to exit nonzero with a diagnostic, and
# tests/warn/ fixtures are supposed to exit 0 with a warning; `make test` already
# scores all of that. Here a fixture fails only if the sanitizer speaks, or if the
# compiler dies on a signal (rc >= 128 — a segfault or an abort). Everything else,
# including a clean nonzero diagnostic exit, is a pass.
#
# ASAN_OPTIONS=detect_leaks=0 — JUSTIFIED, NOT SILENTLY DISABLED
# --------------------------------------------------------------
# tychoc never frees, by design. It is a single-pass, one-shot compiler that leaks
# every AST node it allocates: `xmalloc` of every Expr/Stmt/Proc, every `sfmt`
# string, every generic bind vector (`gi.binds`, src/tychoc.c:7585@binds, xmalloc'd and
# never freed — the pattern predates plan.md Phase 37, which followed it). Process
# exit is the deallocator. With detect_leaks=1 every single fixture would report
# hundreds of "leaks" that are the intended allocation discipline, and a real
# overrun would be one line in that flood. So leak detection is OFF here and stays
# a non-goal for this binary.
#   NOTE: this is the opposite of tests/run.sh:49-50, which keeps detect_leaks=1
#   for the EMITTED programs. That is correct there and correct here: emitted
#   programs run under the implicit-arena model where every scope frees its arena,
#   so a leak there is a real missing-arena-free bug. The compiler has no arenas.
# ASan and UBSan themselves are fully on, with -fno-sanitize-recover=all, so the
# first fault is fatal and cannot be walked past.
#
# LD_PRELOAD
# ----------
# A dev shell on this machine exports LD_PRELOAD=/home/igzo/phonic/tools/block-nnp.so.
# Any such foreign preload loads before libasan.so and makes an ASan binary abort
# at startup with "ASan runtime does not come first in initial library list" —
# a property of that unrelated preload, not of tycho. The lane unsets LD_PRELOAD
# for its own child processes so the gate measures the compiler rather than the
# shell it was launched from, and says so out loud when it had to. It does NOT set
# verify_asan_link_order=0: that check must stay live for real link-order bugs.
#
# COVERAGE — what is in, and what is NOT
# -------------------------------------
# IN:  examples/*.ty, tests/*.ty, tests/pkg/*/main.ty,
#      tests/reject/*.ty, tests/reject/pkg/*/main.ty, tests/abort/*.ty,
#      tests/diag/*.ty, tests/warn/*.ty, tests/conc/*.ty — the same corpus
#      `make test` and `make conc` score — plus the four largest real Tycho
#      programs in the tree (compiler/tychoc0.ty, tools/tycho.ty,
#      tools/tychofmt.ty, tools/lsp.ty), deeper shapes than any single fixture.
# NOT: corelib/ and examples/corelib/ (their harnesses have per-module dependency
#      skips this lane deliberately does not replicate); the fuzz corpora
#      (generated, not committed); -m32 (no 32-bit ASan runtime under multilib,
#      same reason Makefile:270@SKIPPED skips it); and the emitted programs' own runtime
#      behaviour (tests/run.sh owns that).
#
# HISTORY: from 2026-07-29 the IN list and the glob below also named
# tests/postfreeze/*.ty, a directory that held fixtures the FROZEN tychoc0 could
# not parse. Its two lanes (compiler/fixpoint.sh, scripts/frontparity.sh) were
# retired later the same day and the directory was folded back into tests/, so
# those fixtures are covered by tests/*.ty here and its one abort fixture by
# tests/abort/*.ty — which the postfreeze glob never reached, so this lane's
# corpus grew by one file in the fold. The binary under test is and always was
# the LIVE compiler: :110-111 builds src/tychoc.c with -fsanitize, and
# compiler/tychoc0.ty appears in the glob below as a SUBJECT file, never as the
# compiler.
#
# The whole lane runs in well under a minute on the measured host, so it is wired
# into scripts/ci.sh unconditionally with no subsetting.
#
# Exit status: 0 iff no fixture made the sanitizer speak and none crashed.
set -u
cd "$(dirname "$0")/.." || exit 2

CC="${CC:-cc}"
SAN="build/tychoc-asan"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# See the LD_PRELOAD note above: a foreign preload in the launching shell breaks
# every ASan binary before main(). Report it rather than papering over it.
if [ -n "${LD_PRELOAD:-}" ]; then
    echo "asan-self: NOTE — unsetting a foreign LD_PRELOAD='$LD_PRELOAD' for the sanitized child"
    echo "asan-self:        processes (it would load before libasan and abort them at startup)."
    unset LD_PRELOAD
fi
export ASAN_OPTIONS=detect_leaks=0
export UBSAN_OPTIONS=print_stacktrace=1

echo "asan-self: building $SAN  (ASan+UBSan, -fno-sanitize-recover=all)"
mkdir -p build
if ! $CC -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1 -fwrapv \
        -std=c11 -Ibuild src/tychoc.c -o "$SAN" 2>"$TMP/build.log"; then
    echo "asan-self: FATAL — could not build the sanitized compiler"
    sed 's/^/      /' "$TMP/build.log"
    exit 2
fi

pass=0
fail=0
fails=""

# check_one <entry.ty> <label>
check_one() {
    hi="$1"; name="$2"
    "$SAN" "$hi" --emit-c -o "$TMP/out" >"$TMP/log" 2>&1
    rc=$?
    # The sanitizer's own voice. tychoc's diagnostics are `file:LINE: error: MSG`
    # (lowercase), so they cannot collide with `ERROR: ` here; the only
    # "runtime error" string in src/tychoc.c is a comment (:8320), never output.
    if grep -qE 'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:|ERROR: ' "$TMP/log"; then
        echo "FAIL  $name  (sanitizer report)"
        sed 's/^/      /' "$TMP/log"
        fail=$((fail + 1)); fails="$fails $name"
    elif [ "$rc" -ge 128 ]; then
        # Killed by a signal. A rejected program exits 1 with a diagnostic; a
        # SIGSEGV/SIGABRT is the compiler falling over, which is a finding even
        # when the sanitizer did not get a word in first.
        echo "FAIL  $name  (compiler died on signal, rc=$rc)"
        sed 's/^/      /' "$TMP/log"
        fail=$((fail + 1)); fails="$fails $name"
    else
        pass=$((pass + 1))
    fi
    rm -f "$TMP/out.c"
}

for hi in examples/*.ty tests/*.ty tests/conc/*.ty \
          tests/reject/*.ty tests/abort/*.ty tests/diag/*.ty tests/warn/*.ty \
          compiler/tychoc0.ty tools/tycho.ty tools/tychofmt.ty tools/lsp.ty; do
    [ -e "$hi" ] || continue
    check_one "$hi" "$hi"
done
for d in tests/pkg/*/ tests/reject/pkg/*/; do
    [ -d "$d" ] || continue
    [ -f "${d}main.ty" ] || continue
    check_one "${d}main.ty" "${d}main.ty"
done

echo "-----------------------------------------"
echo "asan-self: compiled: $pass   failed: $fail"
[ "$fail" -eq 0 ] || { echo "failed:$fails"; exit 1; }
echo "asan-self: all green (tychoc's own execution is ASan+UBSan clean over the corpus)"
