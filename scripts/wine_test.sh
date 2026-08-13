#!/bin/sh
# Phase-3 wine-test: the plain fixture corpus, cross-compiled and run under
# Wine against the Linux goldens. The Linux-box approximation of "make test
# on the Windows box" (plan_windows.md phase 3); scripts/wine_smoke.sh covers
# the concurrency + stack-guard lanes, this covers everything tests/run.sh's
# main loop does: the plain corpus (examples/*.ty tests/*.ty + stdin + golden),
# the package programs (tests/pkg/*/main.ty), the runtime aborts (must die
# cleanly with a 'tycho:' message), and the compiler-diagnostics goldens
# (tests/diag/*.ty -- the MINGW compiler's stderr must match byte-for-byte).
#
#   sh scripts/wine_test.sh              # the whole corpus (~20 min)
#   WINE_TEST_FILTER=name sh scripts/wine_test.sh   # a substring filter
#
# NOT a gate and NOT a Windows verdict -- Wine is an approximation, and the
# output's real value is the PARK LIST: fixtures that redden only for
# Windows-environment reasons (paths, shell-out via cmd.exe, file semantics)
# get parked for phase 6's golden audit instead of patched. Skips loudly when
# the mingw cross compiler or wine is absent.
set -u
cd "$(dirname "$0")/.." || exit 2
MINGWCC="$(command -v x86_64-w64-mingw32-gcc || true)"
# Wine 9.0 merged wine64 into a single 64-bit `wine`, and Arch/CachyOS ships
# wine 11.x with no wine64 binary at all -- so a bare `command -v wine64` made
# this lane SKIP on every modern Wine while printing a reason that read like a
# missing install. Prefer wine64 when it exists (older split installs), else
# wine. Measured 2026-08-09 on this box: wine-11.14, no wine64.
WINE="$(command -v wine64 || command -v wine || true)"
[ -n "$WINE" ] || { echo "SKIP: neither wine64 nor wine on PATH"; exit 0; }
[ -n "$MINGWCC" ] || { echo "SKIP: x86_64-w64-mingw32-gcc not on PATH"; exit 0; }
export LD_PRELOAD=
FILTER="${WINE_TEST_FILTER:-}"

T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0; pass=0
park=""
note() { echo "      $*"; }

mkdir -p build
make -s build/tycho_rt_embed.h
# REBUILD WHEN THE SOURCE MOVED, not merely when the exe is absent. Until
# 2026-08-13 this said `if [ ! -x ... ]`, so an exe cross-built once was reused
# for ever: on this box it was 8 days old and 25 fixtures "failed" at emit/cc
# under mingw purely because the compiler predated the features they use. A lane
# whose subject is stale reports on a program nobody is running.
if [ ! -x build/tychoc-mingw.exe ] \
   || [ src/tychoc.c -nt build/tychoc-mingw.exe ] \
   || [ build/tycho_rt_embed.h -nt build/tychoc-mingw.exe ]; then
    "$MINGWCC" -O2 -fwrapv -std=c11 -Ibuild src/tychoc.c -o build/tychoc-mingw.exe \
        || { echo "FAIL: mingw compiler build"; exit 2; }
fi
# the mingw compiler finds corelib next to its own exe via argv0; under wine
# that is the repo build/ dir, so point it at the real corelib explicitly
CORELIB="Z:\\$(pwd | sed 's|^/||; s|/|\\|g')\\corelib"
E="env -u LD_PRELOAD WINEDEBUG=-all TYCHO_CORELIB=$CORELIB $WINE ./build/tychoc-mingw.exe"
W="env -u LD_PRELOAD WINEDEBUG=-all WINEPATH=Z:\\usr\\x86_64-w64-mingw32\\lib $WINE"
CCF="-O3 -fwrapv -pthread"          # the transpiler's own Windows cc line

# one positive fixture: emit + cc + wine-run + golden cmp
pos() {
    name="$1"; hi="$2"; g="$3"; in="$4"
    case "$name" in *"$FILTER"*) ;; *) return ;; esac
    $E "$hi" --emit-c -o "$T/$name" >"$T/$name.emit" 2>&1 || { note "$name (emit)"; fail=$((fail + 1)); return; }
    # THE SHIMS. tychoc links a package's `<pkg>_shim.c` itself (merge_pkg calls
    # add_shim); this lane compiled the emitted C alone, so any fixture importing
    # a shim-backed package died at LINK and was read as a port failure --
    # p_corelib_variant_shadow, "undefined reference to strx_parse_double",
    # 2026-08-13. The Linux compiler is asked for the list: the import closure is
    # the same source and the same corelib, and its answer is a Linux path the
    # cross compiler can read, which the wine exe's own Z:\ answer is not.
    _shims=$(./tychoc "$hi" --print-shims 2>/dev/null | tr '\n' ' ')
    "$MINGWCC" $CCF -o "$T/$name.exe" "$T/$name.c" $_shims -lm 2>"$T/$name.cc" || { note "$name (cc)"; fail=$((fail + 1)); return; }
    $W "$T/$name.exe" <"$in" >"$T/$name.out" 2>/dev/null; rc=$?
    if [ "$rc" -ne 0 ]; then note "$name (exit $rc)"; fail=$((fail+1)); return; fi
    if cmp -s "$T/$name.out" "$g"; then pass=$((pass+1))
    else
        # classify: show only the first differing line for the park list
        d=$(diff "$g" "$T/$name.out" 2>/dev/null | head -2 | tr '\n' ' ')
        note "$name (mismatch: $d)"
        park="$park $name"
        fail=$((fail+1))
    fi
}

echo ">>> plain corpus + examples (byte-identical vs goldens)"
for hi in examples/*.ty tests/*.ty; do
    [ -e "$hi" ] || continue
    n="$(basename "$hi" .ty)"
    in="tests/$n.in"; [ -f "$in" ] || in=/dev/null
    pos "c_$n" "$hi" "tests/$n.out" "$in"
done
echo ">>> package programs (tests/pkg/*/main.ty)"
for d in tests/pkg/*/; do
    [ -d "$d" ] || continue
    n="$(basename "$d")"
    case "$n" in *"$FILTER"*) ;; *) continue ;; esac
    pos "p_$n" "$d/main.ty" "tests/pkg/$n.out" /dev/null
done
echo ">>> runtime aborts (must die cleanly with a 'tycho:' message)"
for hi in tests/abort/*.ty; do
    [ -e "$hi" ] || continue
    n="abort_$(basename "$hi" .ty)"
    case "$n" in *"$FILTER"*) ;; *) continue ;; esac
    $E "$hi" --emit-c -o "$T/ab" >/dev/null 2>&1 || { note "$n (emit)"; fail=$((fail+1)); continue; }
    _shims=$(./tychoc "$hi" --print-shims 2>/dev/null | tr '\n' ' ')
    "$MINGWCC" $CCF -o "$T/ab.exe" "$T/ab.c" $_shims -lm 2>/dev/null || { note "$n (cc)"; fail=$((fail+1)); continue; }
    $W "$T/ab.exe" </dev/null >/dev/null 2>"$T/ab.err"; rc=$?
    # A fixture MAY lock its exact stderr in a sibling .err instead of carrying a
    # `tycho:` prefix -- tests/run.sh:381 has had that rule; this lane did not, so
    # tests/abort/main_result_err.ty (whose message is the PROGRAM's, and whose
    # own header says there is no `tycho:` and should not be) was read as a
    # Windows failure on 2026-08-13. Same rule here now.
    _errg="tests/abort/$(basename "$hi" .ty).err"
    if [ "$rc" -eq 0 ]; then note "$n (did not die)"; fail=$((fail+1))
    elif [ -f "$_errg" ]; then
        if cmp -s "$T/ab.err" "$_errg"; then pass=$((pass+1))
        else note "$n (stderr differs from $_errg)"; fail=$((fail+1)); fi
    elif grep -q "tycho:" "$T/ab.err"; then pass=$((pass+1))
    else note "$n (died rc=$rc, no tycho: msg)"; fail=$((fail+1)); fi
done
echo ">>> compiler diagnostics (mingw compiler stderr vs tests/diag/*.err)"
for hi in tests/diag/*.ty; do
    [ -e "$hi" ] || continue
    n="diag_$(basename "$hi" .ty)"
    case "$n" in *"$FILTER"*) ;; *) continue ;; esac
    $E "$hi" --emit-c -o "$T/dg" >"$T/dg.out" 2>"$T/dg.err"; rc=$?
    if [ "$rc" -eq 0 ]; then note "$n (compiler ACCEPTED invalid program)"; fail=$((fail+1))
    elif cmp -s "$T/dg.err" "tests/diag/$(basename "$hi" .ty).err"; then pass=$((pass+1))
    else
        d=$(diff "tests/diag/$(basename "$hi" .ty).err" "$T/dg.err" 2>/dev/null | head -2 | tr '\n' ' ')
        note "$n (diag differs: $d)"
        park="$park $n"; fail=$((fail+1))
    fi
done

echo
echo "wine-test: passed $pass  failed $fail"
if [ -n "$park" ]; then
    echo "park candidates (Windows-environment differences, phase 6 audit):"
    echo "$park" | tr ' ' '\n' | grep -v '^$' | sed 's/^/  /'
fi
[ "$fail" -eq 0 ] || { echo "wine-test: FAIL ($fail)"; exit 1; }
echo "wine-test: all green (approximation only)"
