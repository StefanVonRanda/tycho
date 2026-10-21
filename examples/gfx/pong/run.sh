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

"$TYCHOC" main.ty --pkg sdl3 -o "$TMP/pong" || {
    echo "pong: build failed (SDL3 development files and pkg-config sdl3 are required)"
    exit 2
}

SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy "$TMP/pong" --headless --frames 30 \
    --snapshot "$TMP/dummy.bmp" || {
    echo "pong: headless render failed"
    exit 1
}
[ "$(head -c 2 "$TMP/dummy.bmp")" = "BM" ] || {
    echo "pong: snapshot is not a BMP"
    exit 1
}

if command -v Xvfb >/dev/null 2>&1; then
    D=$((900 + ($$ % 800)))
    Xvfb ":$D" -screen 0 960x540x24 -nolisten tcp >/dev/null 2>&1 &
    xp=$!
    sleep 0.5
    if DISPLAY=":$D" SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=x11 "$TMP/pong" --headless --frames 30 \
            --snapshot "$TMP/x11.bmp"; then
        [ "$(head -c 2 "$TMP/x11.bmp")" = "BM" ] || {
            echo "pong: X11 snapshot is not a BMP"
            exit 1
        }
        echo "pong: ok (headless BMP and X11 window)"
    else
        echo "pong: X11 leg failed"
        exit 1
    fi
    kill "$xp" 2>/dev/null
    wait "$xp" 2>/dev/null
    xp=""
else
    echo "pong: ok (headless BMP; Xvfb not installed, X11 leg skipped)"
fi
