#!/bin/sh
set -eu

# Android target smoke: cross-compile every corelib test that has no external
# dependency with the NDK, link a STATIC bionic binary, and run it under
# qemu-user. The emulator is not required; a static Android ELF carries its own
# libc and the qemu user-mode runner supplies the kernel ABI.
#
# Tests whose output depends on the host environment are skipped by name:
#   net      -- no /etc/hosts in the qemu root
#   strings  -- bionic has no comma-decimal locale
# Tests with external C dependencies are skipped because the NDK sysroot does
# not carry them. `os` and `dynlib` have Android-specific goldens.

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$root"

TYCHOC="${TYCHOC:-./tychoc1}"
[ -x "$TYCHOC" ] || { echo "android-check: no ./tychoc1 -- run 'make' first" >&2; exit 2; }

find_ndk() {
    for d in "${ANDROID_NDK_HOME:-}" "${ANDROID_NDK_ROOT:-}"; do
        [ -n "$d" ] && [ -d "$d" ] && { printf '%s\n' "$d"; return; }
    done
    for d in "${ANDROID_HOME:-}/ndk" "${ANDROID_SDK_ROOT:-}/ndk" "$HOME/.android-sdk/ndk"; do
        [ -d "$d" ] && { find "$d" -mindepth 1 -maxdepth 1 -type d | sort -V | tail -1; return; }
    done
    return 1
}

NDK=$(find_ndk) || { echo "android-check: no Android NDK (set ANDROID_NDK_HOME)" >&2; exit 2; }
[ -d "$NDK" ] || { echo "android-check: NDK '$NDK' is not a directory" >&2; exit 2; }
PREBUILT=$(find "$NDK/toolchains/llvm/prebuilt" -mindepth 1 -maxdepth 1 -type d | head -1)
[ -n "$PREBUILT" ] || { echo "android-check: NDK has no llvm prebuilt toolchain" >&2; exit 2; }

ABI="${ANDROID_ABI:-x86_64}"
case "$ABI" in
    x86_64)  TRIPLE=x86_64-linux-android;  QEMU="${QEMU_X86_64:-qemu-x86_64-static}" ;;
    aarch64|arm64) TRIPLE=aarch64-linux-android; QEMU="${QEMU_AARCH64:-qemu-aarch64-static}" ;;
    *) echo "android-check: unsupported ANDROID_ABI '$ABI' (want x86_64 or aarch64)" >&2; exit 2 ;;
esac
API="${ANDROID_API:-34}"
CC="$PREBUILT/bin/$TRIPLE$API-clang"
[ -x "$CC" ] || { echo "android-check: missing NDK compiler '$CC'" >&2; exit 2; }
command -v "$QEMU" >/dev/null 2>&1 || { echo "android-check: missing '$QEMU'" >&2; exit 2; }

T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

SKIP="bzip2 compress compress_filters compress_legacy crypto gfx gfx_window gfxgpu http image net sqlite strings tls x509 xz"
ran=0
skipped=0
fail=0

for entry in corelib/test/*/main.ty; do
    [ -e "$entry" ] || continue
    name=$(basename "$(dirname "$entry")")
    case " $SKIP " in
        *" $name "*) skipped=$((skipped + 1)); continue ;;
    esac
    deps=$("$TYCHOC" "$entry" --print-deps 2>/dev/null || true)
    if [ -n "$deps" ]; then
        echo "skip $name (external dependency: $deps)"
        skipped=$((skipped + 1))
        continue
    fi
    if ! TYCHO_CFLAGS=-static "$TYCHOC" "$entry" --cc "$CC" --link dl -o "$T/$name" >"$T/$name.build" 2>&1; then
        echo "FAIL $name (android build)"
        sed 's/^/      /' "$T/$name.build" | tail -20
        fail=1
        continue
    fi
    if ! timeout 60 "$QEMU" "$T/$name" >"$T/$name.raw" 2>&1; then
        echo "FAIL $name (android run)"
        tail -10 "$T/$name.raw" | sed 's/^/      /'
        fail=1
        continue
    fi
    # bionic logs a diagnostic when a deliberately invalid TZ name reaches its
    # tzdata lookup. The test is exercising fallback behavior, not the logger.
    sed '/^__bionic_open_tzdata:/d' "$T/$name.raw" >"$T/$name.out"
    golden="corelib/test/$name.out"
    [ -f "corelib/test/$name.out.android" ] && golden="corelib/test/$name.out.android"
    if ! cmp -s "$golden" "$T/$name.out"; then
        echo "FAIL $name (golden mismatch: $golden)"
        diff -u "$golden" "$T/$name.out" | head -20 | sed 's/^/      /'
        fail=1
        continue
    fi
    ran=$((ran + 1))
done

if [ "$fail" -ne 0 ]; then
    echo "android-check: FAILED ($ran passed, $skipped skipped, $ABI, API $API)" >&2
    exit 1
fi
echo "android-check: green ($ran test(s) on $ABI, API $API, $skipped skipped)"
