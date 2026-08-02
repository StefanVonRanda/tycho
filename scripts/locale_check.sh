#!/bin/sh
# locale-check: build and run the two locale fixtures with the process locale
# actually hostile, which is the only way the three locale fixes can be seen.
#
# WHAT IT GATES -- three sites, fixed across three plans, each of which was a
# correct source file turning into a wrong number with nothing on stderr:
#
#   1. src/tychoc.c@c_strtod   the lexer READS a float literal. A bare strtod
#      under a comma-decimal LC_NUMERIC stops at the '.', so `1.5` becomes 1.
#   2. src/tychoc.c@c_dtoa     codegen WRITES a float literal. A bare
#      snprintf("%.17g") emits the bytes `1,5`; the ".0" guard scans for '.',
#      finds none, appends -- `1,5.0`, which cc reads as a comma expression in
#      expression position and rejects in a declarator initializer.
#   3. runtime/tycho_rt.c@tycho_float_to_str   a RUNNING program's str(float),
#      same "%g" and same ".0" guard, one layer down.
#
# WHY THE ENVIRONMENT ALONE IS NOT THE CHECK -- this is the whole reason this
# script exists rather than an `LC_ALL=...` prefix in scripts/ci.sh. **A C
# program starts in the "C" locale whatever LC_ALL says**, until something calls
# setlocale(LC_ALL, ""), and neither tychoc nor the runtime ever does -- which is
# precisely why all three defects are latent rather than live. So
# `LC_ALL=da_DK.utf8 ./tychoc ...` is INERT: docs/internals/plan-four-found-DONE.md
# phase 1 ran the FULLY BROKEN compiler that way and the fixture came out green.
# A check that cannot fail is not a check.
#
# What reaches it is the trigger the comment above src/tychoc.c@c_numeric_handle
# names: a linked C library calling setlocale from a load-time constructor. This
# script builds that library -- four lines, in a temp dir -- and LD_PRELOADs it.
#
# WHAT IS AND IS NOT LEFT BEHIND. Everything (the .so, the emitted C, the two
# binaries) is written into a `mktemp -d` removed on EXIT. Nothing is written
# into the tree, no golden is re-recorded, and there is no RECORD=1 path: the two
# goldens are owned by tests/run.sh, and this lane is a second, harsher way of
# reading them, not a second owner of them.
#
# LD_PRELOAD. This script REPLACES any inherited LD_PRELOAD for the commands it
# runs, rather than prepending to it, and says so when it finds one. Prepending
# would drag a sanitizer or a tmux shim into a lane whose entire subject is what
# a load-time constructor does -- and CLAUDE.md's Environment section records a
# stale LD_PRELOAD scoring 251/527 spurious failures once already. The inherited
# value is left untouched in the caller's environment; only the child sees ours.
#
# SKIPS ARE LOUD AND NAMED. Three of them -- no locale(1), no comma-decimal
# locale installed, and a preload that builds but does not take effect. Each
# prints its reason and exits 0. A silent skip is the vacuous pass this lane's
# own plan chain hit five times; the third one exists because "the .so compiled"
# is not the same fact as "the .so changed the locale", and only the second is
# the one this gate depends on.
#
# In `make ci` as step [2e/13]. See the locale-check target in the Makefile.
set -u
cd "$(dirname "$0")/.." || exit 2

TYCHOC=./tychoc
CC="${CC:-cc}"
[ -x "$TYCHOC" ] || { echo "locale-check: no ./tychoc -- run 'make' first" >&2; exit 2; }

# --- pick a hostile locale --------------------------------------------------
# The decision input is overridable so the skip path is testable: setting
# TYCHO_LOCALE_CHECK_LOCALES to a list with no comma-decimal member (e.g. "C
# POSIX") reproduces a host that has none, without uninstalling anything.
# Each candidate is CONFIRMED with `locale decimal_point` rather than trusted by
# name -- an uninstalled name silently falls back to "." and would otherwise look
# like a locale we had.
cands="${TYCHO_LOCALE_CHECK_LOCALES-}"
if [ -z "$cands" ]; then
    command -v locale >/dev/null 2>&1 || {
        echo "locale-check: SKIP (no locale(1) on this host -- nothing can tell us which locales use a comma)"
        exit 0
    }
    # The three the evidence was recorded under come first so this lane keeps
    # matching its transcript where they exist; the full list is the fallback.
    cands="da_DK.utf8 da_DK de_DE.utf8 fr_FR.utf8 $(locale -a 2>/dev/null)"
fi
HOSTILE=""
for c in $cands; do
    [ "$(LC_ALL="$c" locale decimal_point 2>/dev/null)" = "," ] || continue
    HOSTILE="$c"; break
done
if [ -z "$HOSTILE" ]; then
    echo "locale-check: SKIP (no comma-decimal locale installed -- none of $(printf '%s ' $cands | wc -w) candidate name(s) reports decimal_point=\",\")"
    exit 0
fi

T="$(mktemp -d)" || exit 2
trap 'rm -rf "$T"' EXIT

# --- build the preload ------------------------------------------------------
cat > "$T/loc.c" <<'EOF'
#include <locale.h>
/* The whole mechanism: a load-time constructor that adopts the environment's
   locale, standing in for the linked C library that would do it in the wild. */
__attribute__((constructor)) static void tycho_locale_check_go(void) {
    setlocale(LC_ALL, "");
}
EOF
if ! $CC -shared -fPIC -o "$T/loc.so" "$T/loc.c" 2>"$T/loc.log"; then
    echo "locale-check: SKIP (cannot build an LD_PRELOAD shared object with '$CC -shared -fPIC')"
    sed 's/^/      /' "$T/loc.log"
    exit 0
fi

[ -n "${LD_PRELOAD-}" ] && printf 'locale-check: note: an LD_PRELOAD was already set (%s); it is REPLACED, not extended, for this lane only\n' "$LD_PRELOAD"

# hostile: run one command with the preload in and the inherited value out.
hostile() { env -u LD_PRELOAD LC_ALL="$HOSTILE" LD_PRELOAD="$T/loc.so" "$@"; }

# --- prove the preload actually took effect ---------------------------------
# "The .so compiled" and "the .so changed the decimal separator" are different
# facts. Without this the lane would pass on a host where LD_PRELOAD is stripped
# (setuid, a hardened loader, a container without the locale data), which is the
# exact shape of vacuous pass this gate is here to refuse.
printf '#include <stdio.h>\nint main(void){ printf("%%g\\n", 1.5); return 0; }\n' > "$T/probe.c"
if ! $CC -o "$T/probe" "$T/probe.c" 2>"$T/probe.log"; then
    echo "locale-check: SKIP (cannot build the C probe that verifies the preload)"
    sed 's/^/      /' "$T/probe.log"
    exit 0
fi
got="$(hostile "$T/probe")"
if [ "$got" != "1,5" ]; then
    echo "locale-check: SKIP (LD_PRELOAD did not take effect: printf(\"%g\", 1.5) under LC_ALL=$HOSTILE gave '$got', wanted '1,5')"
    exit 0
fi

echo "locale-check: hostile locale $HOSTILE (decimal_point=\",\"), preload verified"

# --- the lane ---------------------------------------------------------------
fail=0
for name in float_lit_locale float_str_locale; do
    src="tests/$name.ty"
    golden="tests/$name.out"
    if [ ! -f "$src" ] || [ ! -f "$golden" ]; then
        echo "FAIL: $name -- $src or $golden is missing"; fail=1; continue
    fi

    # (1) COMPILE under the hostile locale. This is the leg no other gate has:
    #     tests/run.sh runs tychoc in whatever locale the grader has, which is
    #     "C" -- the case that never broke. Both compiler sites are here.
    if ! "$TYCHOC" "$src" --emit-c -o "$T/${name}_c" >"$T/$name.log" 2>&1; then
        echo "FAIL: $name -- tychoc failed in the DEFAULT locale (not a locale bug)"
        sed 's/^/      /' "$T/$name.log"; fail=1; continue
    fi
    if ! hostile "$TYCHOC" "$src" --emit-c -o "$T/${name}_h" >"$T/$name.log" 2>&1; then
        echo "FAIL: $name -- tychoc failed under LC_ALL=$HOSTILE"
        sed 's/^/      /' "$T/$name.log"; fail=1; continue
    fi

    # (2) The emitted C must be BYTE-IDENTICAL between the two locales. This is
    #     the sharpest statement of the compiler half: a read-side or write-side
    #     regression moves these bytes whether or not cc later objects, and the
    #     diff names the literal that moved.
    #
    #     It records the failure and DOES NOT `continue`. Skipping ahead here
    #     would make leg (3) unreachable for the only defect that produces it:
    #     write-side breakage always moves these bytes, so an early exit would
    #     leave `cc REJECTED` as a leg that cannot fire -- a check that cannot
    #     fail, in the gate whose whole subject is checks that cannot fail.
    if ! cmp -s "$T/${name}_c.c" "$T/${name}_h.c"; then
        echo "FAIL: $name -- emitted C DIFFERS between the default and the $HOSTILE locale (src/tychoc.c: c_strtod / c_dtoa)"
        diff -u "$T/${name}_c.c" "$T/${name}_h.c" | grep '^[-+][^-+]' | head -8 | sed 's/^/      /'
        fail=1
    fi

    # (3) cc must accept the hostile-locale C. `double h_a = 1,5.0;` is a syntax
    #     error in a declarator initializer -- the loud third of the write-side
    #     defect, kept as its own named leg because (2) reports it as a diff and
    #     this reports it as the compiler error the user would actually see.
    if ! $CC -O2 -fwrapv -std=c11 -o "$T/$name.bin" "$T/${name}_h.c" -lm 2>"$T/$name.cc.log"; then
        echo "FAIL: $name -- cc REJECTED the C emitted under LC_ALL=$HOSTILE"
        head -4 "$T/$name.cc.log" | sed 's/^/      /'; fail=1; continue
    fi

    # (4) RUN it, in both locales, against the same golden. The hostile run is
    #     the runtime site: with a comma locale already live in the process, the
    #     fixture's "-- default environment --" block is hostile too, so every
    #     str(float) in it goes through runtime/tycho_rt.c@tycho_float_to_str
    #     with LC_NUMERIC set against it.
    #     Both runs are of the binary built from the HOSTILE-locale C above --
    #     that is the artifact under test -- so the leg names say which locale
    #     the RUN was in, not which one the build was in.
    "$T/$name.bin" > "$T/$name.plain" 2>&1
    hostile "$T/$name.bin" > "$T/$name.hostile" 2>&1
    if ! cmp -s "$T/$name.plain" "$golden"; then
        echo "FAIL: $name -- built under $HOSTILE, RUN in the default locale: differs from $golden"
        diff -u "$golden" "$T/$name.plain" | head -12 | sed 's/^/      /'; fail=1; continue
    fi
    if ! cmp -s "$T/$name.hostile" "$golden"; then
        echo "FAIL: $name -- built under $HOSTILE, RUN under LC_ALL=$HOSTILE: differs from $golden (runtime/tycho_rt.c@tycho_float_to_str)"
        diff -u "$golden" "$T/$name.hostile" | head -12 | sed 's/^/      /'; fail=1; continue
    fi

    echo "  ok $name (emitted C identical in both locales; binary == golden in both)"
done

if [ "$fail" -ne 0 ]; then
    echo "locale-check: FAILED"
    exit 1
fi
echo "locale-check: ok (2 fixtures compiled AND run under LC_ALL=$HOSTILE with setlocale forced by LD_PRELOAD)"
