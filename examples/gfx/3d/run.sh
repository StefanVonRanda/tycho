#!/bin/sh
set -u
cd "$(dirname "$0")" || exit 2
TYCHOC="${TYCHOC:-../../../tychoc1}"
[ -x "$TYCHOC" ] || { echo "no $TYCHOC — run make at the repo root"; exit 2; }

TMP="$(mktemp -d)"
xp=""
cleanup() {
    [ -n "$xp" ] && kill "$xp" 2>/dev/null
    rm -rf "$TMP"
}
trap cleanup EXIT HUP INT TERM

"$TYCHOC" main.ty --pkg sdl3 -o "$TMP/vault" || {
    echo "3d: build failed (SDL3 development files and pkg-config sdl3 are required)"
    exit 2
}

SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy "$TMP/vault" --headless --frames 30 \
    --snapshot "$TMP/dummy.bmp" || {
    echo "3d: headless render failed"
    exit 1
}
[ "$(head -c 2 "$TMP/dummy.bmp")" = "BM" ] || {
    echo "3d: snapshot is not a BMP"
    exit 1
}

if command -v Xvfb >/dev/null 2>&1; then
    D=$((1200 + ($$ % 500)))
    Xvfb ":$D" -screen 0 1280x720x24 -nolisten tcp >/dev/null 2>&1 &
    xp=$!
    sleep 0.5
    if DISPLAY=":$D" SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=x11 "$TMP/vault" --frames 5 \
            --snapshot "$TMP/x11.bmp"; then
        [ "$(head -c 2 "$TMP/x11.bmp")" = "BM" ] || {
            echo "3d: X11 snapshot is not a BMP"
            exit 1
        }
        echo "3d: ok (headless BMP and X11 window)"
    else
        echo "3d: X11 leg failed"
        exit 1
    fi
    kill "$xp" 2>/dev/null
    wait "$xp" 2>/dev/null
    xp=""
else
    echo "3d: ok (headless BMP; Xvfb not installed, X11 leg skipped)"
fi
