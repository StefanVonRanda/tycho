#!/bin/sh
# Build a self-contained binary release tarball for the current platform.
#
#   scripts/release.sh v0.1.1
#
# Produces  dist/tycho-<version>-<os>-<arch>.tar.gz  plus a .sha256, containing the
# compiler, the tools, and the core library laid out so the compiler finds corelib
# beside itself (no TYCHO_CORELIB needed). Publishing is a separate, manual step:
#
#   gh release create <version> dist/tycho-*.tar.gz dist/tycho-*.sha256 --notes-file RELEASE_NOTES.md
#
# There is no hosted CI by policy, so releases are built and published by hand, one
# platform per machine.
set -eu

version="${1:-}"
if [ -z "$version" ]; then
    echo "usage: scripts/release.sh <version>   (e.g. v0.1.1)" >&2
    exit 2
fi

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

os="$(uname -s | tr '[:upper:]' '[:lower:]')"
arch="$(uname -m)"
name="tycho-${version}-${os}-${arch}"
stage="dist/${name}"

echo ">> building compiler + tools"
make -s tychoc tools

echo ">> staging $stage"
rm -rf "$stage"
mkdir -p "$stage"
cp tychoc tychofmt tycho-lsp "$stage"/
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
