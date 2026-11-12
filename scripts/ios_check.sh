#!/bin/sh
set -eu

# iOS target smoke. Runs on macOS with Xcode installed. Builds the
# dependency-free corelib tests for the iOS Simulator SDK, runs them in a booted
# simulator through a small chdir/exec runner, and compiles the same program for
# the device SDK as a build-only check.
#
# Platform-restricted tests are skipped by name:
#   os        -- iOS has no shell, so system/popen are unavailable by design
#   io        -- simulator readlink on the test's seeded link is restricted
#   net_unix  -- simulator does not permit the Unix-socket test
#   signal    -- simulator does not permit kill() on the test's process
#   terminal  -- simulator pty allocation is not stable under the runner
# Tests with external C dependencies are skipped because the iOS SDK does not
# carry them.

[ "$(uname -s)" = Darwin ] || { echo "ios-check: run this on macOS with Xcode" >&2; exit 2; }

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$root"

TYCHOC="${TYCHOC:-./tychoc1}"
[ -x "$TYCHOC" ] || { echo "ios-check: no ./tychoc1 -- run 'make' first" >&2; exit 2; }

if [ -z "${DEVELOPER_DIR:-}" ]; then
    for d in /Applications/Xcode*.app/Contents/Developer; do
        [ -d "$d" ] && { DEVELOPER_DIR="$d"; break; }
    done
fi
[ -n "${DEVELOPER_DIR:-}" ] && [ -d "$DEVELOPER_DIR" ] || {
    echo "ios-check: no Xcode (set DEVELOPER_DIR)" >&2
    exit 2
}
export DEVELOPER_DIR

xcrun --sdk iphonesimulator --show-sdk-path >/dev/null 2>&1 || {
    echo "ios-check: Xcode has no iOS Simulator SDK" >&2
    exit 2
}
xcrun --sdk iphoneos --show-sdk-path >/dev/null 2>&1 || {
    echo "ios-check: Xcode has no iOS device SDK" >&2
    exit 2
}

T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

cat >"$T/ios-sim-clang" <<'EOF'
#!/bin/sh
exec xcrun --sdk iphonesimulator clang -arch arm64 -mios-simulator-version-min=17.0 "$@"
EOF
cat >"$T/ios-device-clang" <<'EOF'
#!/bin/sh
exec xcrun --sdk iphoneos clang -arch arm64 -mios-version-min=17.0 "$@"
EOF
chmod +x "$T/ios-sim-clang" "$T/ios-device-clang"

cat >"$T/ios-runner.c" <<'EOF'
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s cwd program [args...]\n", argv[0]); return 2; }
    if (chdir(argv[1]) != 0) { perror("chdir"); return 3; }
    const char *root = getenv("SIMULATOR_ROOT");
    if (root) setenv("DYLD_ROOT_PATH", root, 1);
    execv(argv[2], &argv[2]);
    perror("execv");
    return 4;
}
EOF
"$T/ios-sim-clang" "$T/ios-runner.c" -o "$T/ios-runner"

DEVICE="${IOS_SIM_DEVICE:-iPhone 17}"
if ! xcrun simctl list devices booted | grep -q "Booted"; then
    xcrun simctl boot "$DEVICE" >/dev/null 2>&1 || true
    xcrun simctl bootstatus "$DEVICE" -b >/dev/null 2>&1 || {
        echo "ios-check: could not boot simulator '$DEVICE'" >&2
        exit 2
    }
fi

# Runtime smoke: the platform predicates and a C-to-Tycho callback.
mkdir -p "$T/smoke"
cat >"$T/smoke/main.ty" <<'EOF'
package main
import "core:dynlib"
import "core:os"
import "core:thread"
import "core:time"

fn cb_add(a: int, b: int) -> int:
    return a + b

fn main():
    println("platform=" + str(os.platform()))
    println("ios=" + str(os.is_ios()))
    println("macos=" + str(os.is_macos()))
    println("cpus=" + str(thread.cpus() > 0))
    println("now=" + str(time.unix_secs() > 0))
    println("id=" + str(thread.id() != 0))
    println("shell=" + str(os.system("true")))
    println("callback=" + str(dynlib.apply_i32_i32(cb_add, 20, 22) == 42))
EOF
TYCHO_CORELIB="$root/corelib" "$TYCHOC" "$T/smoke/main.ty" \
    --cc "$T/ios-sim-clang" -o "$T/smoke/run" >/dev/null 2>&1 || {
    echo "ios-check: simulator smoke build failed" >&2
    exit 1
}
if ! timeout 60 xcrun simctl spawn booted "$T/ios-runner" "$root" "$T/smoke/run" >"$T/smoke.raw" 2>&1; then
    echo "ios-check: simulator smoke run failed" >&2
    exit 1
fi
sed '/^Install Started$/d;/^Install Failed: Authorization is required to install the packages.$/d' \
    "$T/smoke.raw" >"$T/smoke.out"
cat >"$T/smoke.expected" <<'EOF'
platform=os__IOS
ios=true
macos=false
cpus=true
now=true
id=true
shell=-1
callback=true
EOF
if ! cmp -s "$T/smoke.expected" "$T/smoke.out"; then
    echo "ios-check: simulator smoke output mismatch" >&2
    diff -u "$T/smoke.expected" "$T/smoke.out" | sed 's/^/      /'
    exit 1
fi

# Build-only: the device SDK must accept the same emitted C.
mkdir -p "$T/device"
cat >"$T/device/main.ty" <<'EOF'
package main
fn main():
    println("ios device build")
EOF
TYCHO_CORELIB="$root/corelib" "$TYCHOC" "$T/device/main.ty" \
    --cc "$T/ios-device-clang" -o "$T/device/run" >/dev/null 2>&1 || {
    echo "ios-check: device-SDK build failed" >&2
    exit 1
}

SKIP="bzip2 compress compress_filters compress_legacy crypto gfx gfx_window gfxgpu http image io net_unix os signal sqlite terminal tls x509 xz zip"
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
        skipped=$((skipped + 1))
        continue
    fi
    if ! TYCHO_CORELIB="$root/corelib" "$TYCHOC" "$entry" --cc "$T/ios-sim-clang" -o "$T/$name" >"$T/$name.build" 2>&1; then
        echo "FAIL $name (iOS build)"
        tail -20 "$T/$name.build" | sed 's/^/      /'
        fail=1
        continue
    fi
    if ! timeout 60 xcrun simctl spawn booted "$T/ios-runner" "$root" "$T/$name" >"$T/$name.raw" 2>&1; then
        echo "FAIL $name (iOS run)"
        tail -10 "$T/$name.raw" | sed 's/^/      /'
        fail=1
        continue
    fi
    # simctl itself prints this pair on every spawn when a platform package is
    # not installed system-wide. It is host chatter, not test output.
    sed '/^Install Started$/d;/^Install Failed: Authorization is required to install the packages.$/d' \
        "$T/$name.raw" >"$T/$name.out"
    if ! cmp -s "corelib/test/$name.out" "$T/$name.out"; then
        echo "FAIL $name (golden mismatch)"
        diff -u "corelib/test/$name.out" "$T/$name.out" | head -20 | sed 's/^/      /'
        fail=1
        continue
    fi
    ran=$((ran + 1))
done

if [ "$fail" -ne 0 ]; then
    echo "ios-check: FAILED ($ran passed, $skipped skipped)" >&2
    exit 1
fi
echo "ios-check: green ($ran simulator test(s), device-SDK build, $skipped skipped)"
