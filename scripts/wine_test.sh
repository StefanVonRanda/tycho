set -u
cd "$(dirname "$0")/.." || exit 2
MINGWCC="$(command -v x86_64-w64-mingw32-gcc || true)"
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
CCF="${WINE_CCF:--O3 -fwrapv -pthread}"

# one positive fixture: emit + cc + wine-run + golden cmp
pos() {
    name="$1"; hi="$2"; g="$3"; in="$4"
    case "$name" in *"$FILTER"*) ;; *) return ;; esac
    $E "$hi" --emit-c -o "$T/$name" >"$T/$name.emit" 2>&1 || {
        note "$name (emit)"; sed 's/^/        /' "$T/$name.emit" | head -4
        fail=$((fail + 1)); return; }
    _shims=$(./tychoc "$hi" --print-shims 2>/dev/null | tr '\n' ' ')
    "$MINGWCC" $CCF -o "$T/$name.exe" "$T/$name.c" $_shims -lm 2>"$T/$name.cc" || { note "$name (cc)"; fail=$((fail + 1)); return; }
    $W "$T/$name.exe" <"$in" >"$T/$name.out" 2>/dev/null; rc=$?
    if [ "$rc" -ne 0 ]; then note "$name (exit $rc)"; fail=$((fail+1)); return; fi
    if cmp -s "$T/$name.out" "$g"; then pass=$((pass+1))
    else
        case $name in
            c_float_str_locale)
                sed -e 's/^\(inf  *= inf  rt=\)0$/\11/' \
                    -e 's/^\(-inf  *= -inf  rt=\)0$/\11/' \
                    -e 's/^\(nan  *= nan  rt=\)0$/\11/' \
                    "$T/$name.out" > "$T/$name.win" 2>/dev/null || : ;;
            *) : > "$T/$name.win" 2>/dev/null || : ;;
        esac
        if [ -s "$T/$name.win" ] && cmp -s "$T/$name.win" "$g"; then
            note "$name (ok after the documented Windows difference -- rt on inf/-inf/nan, a C-library probe no Tycho API can reach)"
            pass=$((pass+1))
            return
        fi
        note "$name (mismatch, golden < vs Windows >)"
        diff "$g" "$T/$name.out" 2>/dev/null | head -8 | sed 's/^/        /'
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
    $E "$hi" --emit-c -o "$T/ab" >"$T/ab.emit" 2>&1 || {
        note "$n (emit)"; sed 's/^/        /' "$T/ab.emit" | head -4
        fail=$((fail+1)); continue; }
    _shims=$(./tychoc "$hi" --print-shims 2>/dev/null | tr '\n' ' ')
    "$MINGWCC" $CCF -o "$T/ab.exe" "$T/ab.c" $_shims -lm 2>/dev/null || { note "$n (cc)"; fail=$((fail+1)); continue; }
    $W "$T/ab.exe" </dev/null >/dev/null 2>"$T/ab.err"; rc=$?
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
