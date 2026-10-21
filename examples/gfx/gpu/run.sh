#!/bin/sh
set -u
cd "$(dirname "$0")" || exit 2
TYCHOC="${TYCHOC:-../../../tychoc1}"
[ -x "$TYCHOC" ] || { echo "no $TYCHOC — run make at the repo root"; exit 2; }
PREFIX="${SHADER_PREFIX:-../../../build/shadercross}"
case "$PREFIX" in
  /*) ;;
  *) PREFIX="$PWD/$PREFIX" ;;
esac
if [ -d "$PREFIX/lib64/pkgconfig" ] || [ -d "$PREFIX/lib/pkgconfig" ]; then
    PKG_CONFIG_PATH="$PREFIX/lib64/pkgconfig:$PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    LD_LIBRARY_PATH="$PREFIX/lib64:$PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    DYLD_LIBRARY_PATH="$PREFIX/lib64:$PREFIX/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
    export PKG_CONFIG_PATH LD_LIBRARY_PATH DYLD_LIBRARY_PATH
fi
# A host-installed shadercross (for example the local test install) still needs
# its runtime directory on the loader path.
LD_LIBRARY_PATH="/usr/local/lib64:/usr/local/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LD_LIBRARY_PATH
# Embed the shadercross runtime directory in the probe when we know it. That
# makes a direct `/tmp/gpuprobe` invocation work, not just `run.sh`.
RPATH=""
if [ -d "$PREFIX/lib" ]; then
    RPATH="$RPATH -Wl,-rpath,$PREFIX/lib"
fi
if [ -d "$PREFIX/lib64" ]; then
    RPATH="$RPATH -Wl,-rpath,$PREFIX/lib64"
fi
if [ -d /usr/local/lib64 ]; then
    RPATH="$RPATH -Wl,-rpath,/usr/local/lib64"
fi
if [ -d /usr/local/lib ]; then
    RPATH="$RPATH -Wl,-rpath,/usr/local/lib"
fi
TYCHO_CFLAGS="${TYCHO_CFLAGS:-}$RPATH"
export TYCHO_CFLAGS

TMP="$(mktemp -d)"
xp=""
cleanup() {
    [ -n "$xp" ] && kill "$xp" 2>/dev/null
    rm -rf "$TMP"
}
trap cleanup EXIT HUP INT TERM
"$TYCHOC" main.ty --pkg sdl3 -o "$TMP/gpu" || exit 2
if command -v Xvfb >/dev/null 2>&1; then
    D=$((2200 + ($$ % 300)))
    Xvfb ":$D" -screen 0 1280x720x24 -nolisten tcp >/dev/null 2>&1 &
    xp=$!
    sleep 0.5
    if DISPLAY=":$D" SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=x11 "$TMP/gpu" --frames 30; then
        echo "gpu: ok (X11 SDL_GPU frame)"
    else
        echo "gpu: X11 GPU leg failed"
        exit 1
    fi
    kill "$xp" 2>/dev/null
    wait "$xp" 2>/dev/null
    xp=""
else
    # macOS has no Xvfb; a terminal-launched probe can use the real display.
    if "$TMP/gpu" --frames 30; then
        echo "gpu: ok (native display SDL_GPU frame)"
    else
        echo "gpu: native GPU leg failed"
        exit 1
    fi
fi
