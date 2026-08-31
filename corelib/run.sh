set -u
cd "$(dirname "$0")/.." || exit 2                      # repo root
TYCHOC="${TYCHOC:-./tychoc1}"
[ -x "$TYCHOC" ] || { echo "no ./tychoc -- run 'make' first"; exit 2; }
export TYCHO_CORELIB="$PWD/corelib"
CC="${CC:-cc}"
RECORD="${RECORD:-0}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
ran=0
nskip=0
skipped=""
for entry in corelib/test/*/main.ty; do
    [ -e "$entry" ] || continue
    name="$(basename "$(dirname "$entry")")"
    golden="corelib/test/$name.out"
    if [ "$(uname -s | grep -ciE 'MSYS|MINGW|CYGWIN')" -ne 0 ] && [ -f "corelib/test/$name.out.win" ]; then
        golden="corelib/test/$name.out.win"
    fi
    if ! allpkgs="$("$TYCHOC" "$entry" --print-deps 2>"$T/deps.log")"; then
        echo "FAIL $name (tychoc --print-deps)"; sed 's/^/      /' "$T/deps.log"; fail=1; continue
    fi
    if [ -n "$allpkgs" ]; then
        missing=""
        for pkg in $allpkgs; do pkg-config --exists "$pkg" 2>/dev/null || missing="$missing $pkg"; done
        if [ -n "$missing" ]; then
            echo "skip $name (missing dependency:$missing)"
            nskip=$((nskip + 1)); skipped="$skipped $name(missing:$missing)"; continue
        fi
    fi
    if ! "$TYCHOC" "$entry" -o "$T/c" >/dev/null 2>&1; then echo "FAIL $name (tychoc compile)"; fail=1; continue; fi
    "$T/c" > "$T/co" 2>&1; rc=$?
    # Windows/MSYS2 flake (Prism emulation): under sustained process churn exec
    # of a PE intermittently returns 127. Retry with backoff (Windows only); the
    # Prism startup heap-corruption race (0xC0000374) is per-attempt.
    if [ "$rc" -eq 127 ] && [ "$(uname -s | grep -ciE 'MSYS|MINGW|CYGWIN')" -ne 0 ]; then
        for _try in 1 2 3; do
            sleep 2
            "$T/c" > "$T/co" 2>&1; rc=$?
            [ "$rc" -eq 0 ] && break
        done
    fi
    if [ "$RECORD" = 1 ]; then cp "$T/co" "$golden"; echo "rec  $name"; ran=$((ran + 1)); continue; fi
    if [ ! -f "$golden" ]; then echo "FAIL $name (no golden -- run RECORD=1)"; fail=1; continue; fi
    if ! cmp -s "$T/co" "$golden"; then echo "FAIL $name (output != golden)"; diff "$golden" "$T/co" | head | sed 's/^/      /'; fail=1; continue; fi
    echo "ok   $name"
    ran=$((ran + 1))
done
[ "$fail" -eq 0 ] || { echo "corelib: FAIL"; exit 1; }
if [ "$nskip" -ne 0 ]; then
    echo "corelib: $ran ok, $nskip SKIPPED --$skipped -- NOT all green (those packages were not run)"
else
    echo "corelib: all green ($ran ok, tychoc matches goldens)"
fi
