#!/bin/sh
# Build a self-contained binary release tarball for the current platform.
#
#   scripts/release.sh v0.1.1            native (current OS/arch)
#   scripts/release.sh v0.1.1 --mingw    cross-built Windows (mingw-w64 gcc)
#
# Produces  dist/tycho-<version>-<os>-<arch>.tar.gz  plus a .sha256, containing the
# compiler, the tools, and the core library laid out so the compiler finds corelib
# beside itself (no TYCHO_CORELIB needed). Publishing is a separate, manual step:
#
#   gh release create <version> dist/tycho-*.tar.gz dist/tycho-*.sha256 --notes-file RELEASE_NOTES.md
#
# There is no hosted CI by policy, so releases are built and published by hand, one
# platform per machine. The --mingw leg cross-builds the compiler with
# x86_64-w64-mingw32-gcc (the windows port's phases 1 and 5; the tools join the
# tarball when they compile under mingw -- see plan_windows.md).
set -eu

version="${1:-}"
if [ -z "$version" ]; then
    echo "usage: scripts/release.sh <version> [--mingw]   (e.g. v0.1.1)" >&2
    exit 2
fi
mingw=0
[ "${2:-}" = "--mingw" ] && mingw=1

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

arch="$(uname -m)"

# the version MUST match the compiler's constant (the changelog discipline)
ver="$(./tychoc --version | awk '{print $2}')"
if [ "v$ver" != "$version" ]; then
    echo "!! version mismatch: src/tychoc.c says $ver, release.sh was given $version" >&2
    echo "   bump TYCHO_VERSION in src/tychoc.c and CHANGELOG.md together" >&2
    exit 2
fi

if [ "$mingw" -eq 1 ]; then
    MINGWCC="$(command -v x86_64-w64-mingw32-gcc || true)"
    if [ -z "$MINGWCC" ]; then
        echo "!! --mingw needs the mingw-w64 cross compiler (x86_64-w64-mingw32-gcc)" >&2
        exit 2
    fi
    name="tycho-${version}-mingw64-${arch}"
    stage="dist/${name}"
    echo ">> staging $stage"
    rm -rf "$stage"
    mkdir -p "$stage"
    echo ">> building the compiler with the mingw cross compiler"
    make -s build/tycho_rt_embed.h
    "$MINGWCC" -O2 -fwrapv -std=c11 -Ibuild src/tychoc.c -o "$stage/tychoc.exe" 2>"$root/dist-mingw-build.log" \
        || { echo "!! mingw build failed (see dist-mingw-build.log)" >&2; rm -f "$root/dist-mingw-build.log"; exit 1; }
    rm -f "$root/dist-mingw-build.log"
    # compiler + corelib only for now: the tools join when they cross-compile
    # (plan_windows.md phases 3/5). The mingw compiler emits Windows programs;
    # its cc invocation must point at a Windows-side mingw gcc (--cc).
    cp -r corelib "$stage"/
    cp README.md LICENSE "$stage"/
    mkdir -p "$stage"/examples
    cp examples/hello.ty "$stage"/examples/ 2>/dev/null || true
else
    os="$(uname -s | tr '[:upper:]' '[:lower:]')"
    name="tycho-${version}-${os}-${arch}"
    stage="dist/${name}"

    echo ">> building compiler + tools"
    make -s tychoc tools

    echo ">> staging $stage"
    rm -rf "$stage"
    mkdir -p "$stage"
    cp tychoc tychofmt tycho-lsp tycho-debug "$stage"/
    cp -r corelib "$stage"/
    cp README.md LICENSE "$stage"/
    # a couple of runnable examples so `./tychoc examples/hello.ty` works out of the box
    mkdir -p "$stage"/examples
    cp examples/hello.ty "$stage"/examples/ 2>/dev/null || true

    echo ">> smoke-testing the packaged layout (corelib found beside the binary)"
    tmp="$(mktemp -d)"
    tar -C dist -cf - "$name" | tar -C "$tmp" -xf -
    printf 'fn main():\n    println("release ok")\n' > "$tmp/t.ty"
    ( cd "$tmp/$name" && ./tychoc "$tmp/t.ty" -o "$tmp/t" ) && "$tmp/t" | grep -q "release ok" \
        || { echo "!! packaged compiler failed its smoke test" >&2; rm -rf "$tmp"; exit 1; }
    rm -rf "$tmp"
fi

echo ">> compressing"
tar -C dist -czf "dist/${name}.tar.gz" "$name"
rm -rf "$stage"
( cd dist && { sha256sum "${name}.tar.gz" 2>/dev/null || shasum -a 256 "${name}.tar.gz"; } > "${name}.tar.gz.sha256" )

echo
echo "built: dist/${name}.tar.gz"
cat "dist/${name}.tar.gz.sha256"
echo
echo "publish with:"
echo "  gh release create ${version} dist/${name}.tar.gz dist/${name}.tar.gz.sha256 --notes-file RELEASE_NOTES.md"
