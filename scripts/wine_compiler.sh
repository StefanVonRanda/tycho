#!/bin/sh
# Sourced by the Wine lanes: build the self-hosted compiler for mingw when the
# source is newer than the cross binary.
build_mingw_tychoc() {
    [ -n "${MINGWCC:-}" ] || { echo "FAIL: x86_64-w64-mingw32-gcc not on PATH" >&2; exit 2; }
    [ -x ./tychoc ] || make -s tychoc >/dev/null 2>&1 || { echo "FAIL: host tychoc build" >&2; exit 2; }
    if [ -x build/tychoc-mingw.exe ] \
       && [ -z "$(find compiler -name '*.ty' -newer build/tychoc-mingw.exe -print -quit)" ]; then
        return 0
    fi
    mkdir -p build
    ./tychoc compiler/main.ty --emit-c -o build/tychoc-mingw >/dev/null 2>&1 \
        || { echo "FAIL: mingw compiler emit" >&2; exit 2; }
    shims="$(./tychoc compiler/main.ty --print-shims 2>/dev/null | tr '\n' ' ')"
    # shellcheck disable=SC2086
    "$MINGWCC" -O2 -fwrapv -std=c11 -static -pthread -Wl,--no-insert-timestamp \
        -Icorelib -o build/tychoc-mingw.exe build/tychoc-mingw.c $shims -lm \
        || { echo "FAIL: mingw compiler build" >&2; exit 2; }
    rm -f build/tychoc-mingw.c
}
