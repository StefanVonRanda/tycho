set -eu

usage() {
    echo "usage: scripts/release.sh <version> [--mingw]   (e.g. v0.1.1)" >&2
}

version="${1:-}"
mingw=0
case "$#:${2:-}" in
    1:) ;;
    2:--mingw) mingw=1 ;;
    *) usage; exit 2 ;;
esac

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

case "$(uname -s)" in
    *MSYS*|*MINGW*|*CYGWIN*) PATH="/usr/bin:$PATH"; EXE=".exe" ;;
    *) EXE="" ;;
esac
export PATH
arch="$(uname -m)"

# Build first: a clean checkout has no ./tychoc, and a previous build may carry
# an older version than src/tychoc.c.
echo ">> building native compiler (tychoc1, self-hosted; tychoc bootstraps it)"
make -s tychoc1

# the version MUST match the compiler's constant (the changelog discipline)
ver="$(./tychoc --version | awk '{print $2}')"
if [ "v$ver" != "$version" ]; then
    echo "!! version mismatch: src/tychoc.c says $ver, release.sh was given $version" >&2
    echo "   bump TYCHO_VERSION in src/tychoc.c and CHANGELOG.md together" >&2
    exit 2
fi
# BOTH archives now ship tychoc1, whose version is its own constant, so the
# check above no longer covers what is actually packaged.
ver1="$(./tychoc1 --version | awk '{print $2}')"
if [ "v$ver1" != "$version" ]; then
    echo "!! version mismatch: compiler/main.ty says $ver1, release.sh was given $version" >&2
    echo "   bump version() in compiler/main.ty alongside src/tychoc.c" >&2
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
    # The Windows archive ships the SELF-HOSTED compiler, as the native leg does.
    # It used to cross-build src/tychoc.c, leaving Windows the only platform on
    # the bootstrap's codegen -- 3.00x slower on bench/treewalk.ty. compiler/main.ty
    # cross-builds unchanged, so it is one more entry in the tools loop.
    echo ">> cross-building the compiler and the tools"
    for spec in "tychoc compiler/main.ty -" \
                "tychofmt tools/tychofmt.ty -" \
                "tycho-lsp tools/lsp.ty tools/lsp_shim.c" \
                "tycho-debug tools/tycho-debug/main.ty tools/tycho-debug/debug_shim.c"; do
        # shellcheck disable=SC2086
        set -- $spec; tname="$1"; tentry="$2"; tshim="$3"
        shimarg=""; [ "$tshim" != "-" ] && shimarg="--shim $tshim"
        # shellcheck disable=SC2086
        ./tychoc "$tentry" $shimarg --emit-c -o "$stage/$tname" >/dev/null \
            || { echo "!! $tname failed to emit C" >&2; exit 1; }
        # shellcheck disable=SC2086
        tshims="$(./tychoc "$tentry" $shimarg --print-shims | tr '\n' ' ')"
        [ "$tshim" != "-" ] && tshims="$tshims $tshim"
        # -static: -pthread alone imports libwinpthread-1.dll, which this archive
        # does not carry -- every .exe shipped before 2026-09-05 fails to start.
        # shellcheck disable=SC2086
        "$MINGWCC" -O2 -fwrapv -static -pthread -o "$stage/$tname.exe" "$stage/$tname.c" $tshims -lm \
            2>"$root/dist-mingw-build.log" \
            || { echo "!! $tname failed to link under mingw (see dist-mingw-build.log)" >&2; exit 1; }
        rm -f "$stage/$tname.c" "$root/dist-mingw-build.log"
        echo "   ok $tname.exe"
    done
    # The mingw compiler emits Windows programs; its cc invocation must point at
    # a Windows-side mingw gcc (--cc), which is why the tarball is MSYS2's job to
    # complete -- see the Windows section of README.md.
    cp -r corelib "$stage"/
    # Same reason as the native leg below: tychoc1 COPIES the runtime in at emit
    # time instead of embedding it, so the file travels with the binary.
    mkdir -p "$stage"/runtime
    cp runtime/tycho_rt.c "$stage"/runtime/
    cp README.md LICENSE "$stage"/
    mkdir -p "$stage"/examples
    cp examples/hello.ty "$stage"/examples/ 2>/dev/null || true

    # Smoke test under Wine when it is here: the staged compiler must find the
    # corelib BESIDE ITSELF (no TYCHO_CORELIB), same property the native leg
    # asserts. It stops at --emit-c because there is no Windows cc under Wine.
    # Wine 9.0 merged wine64 into a single 64-bit `wine`; Arch/CachyOS ships
    # wine 11.x with no wine64 at all, so testing only for wine64 shipped an
    # UNRUN tarball on a box that could have run it. Prefer wine64 (older split
    # installs), else wine.
    WINE="$(command -v wine64 || command -v wine || true)"
    if [ -n "$WINE" ]; then
        echo ">> smoke-testing the packaged layout under Wine"
        tmp="$(mktemp -d)"
        printf 'package main\nimport "core:strings"\nfn main():\n    println(strings.to_upper("release ok"))\n' > "$tmp/t.ty"
        ( cd "$stage" && env -u LD_PRELOAD WINEDEBUG=-all "$WINE" ./tychoc.exe "$tmp/t.ty" --emit-c -o "$tmp/t" >/dev/null 2>&1 ) \
            && grep -q "release ok" "$tmp/t.c" \
            || { echo "!! the staged Windows compiler failed its Wine smoke test" >&2; rm -rf "$tmp"; exit 1; }
        rm -rf "$tmp"
        echo "   ok tychoc.exe emitted C under Wine with corelib beside it"
    else
        echo ">> SKIP the Wine smoke test (neither wine64 nor wine on PATH) -- the tarball is"
        echo "   built but unrun; verify it on a Windows box before publishing"
    fi
else
    os="$(uname -s | tr '[:upper:]' '[:lower:]')"
    name="tycho-${version}-${os}-${arch}"
    stage="dist/${name}"

    echo ">> building tools"
    make -s tychofmt tycho-lsp tycho-debug

    echo ">> staging $stage"
    rm -rf "$stage"
    mkdir -p "$stage"
    # the shipped `tychoc` IS tychoc1 -- users get the self-hosted compiler
    cp "tychoc1$EXE" "$stage/tychoc$EXE"
    cp "tychofmt$EXE" "tycho-lsp$EXE" "tycho-debug$EXE" "$stage"/
    cp -r corelib "$stage"/
    # tychoc1 COPIES the runtime into its output at emit time rather than
    # embedding it the way src/tychoc.c does, so the file has to travel with the
    # binary. driver.ty@write_runtime already falls back to <dir-of-argv0>/runtime,
    # as corelib_root does; without this the packaged compiler dies on the first
    # program a user compiles. The mingw archive ships tychoc1 too, and stages
    # the runtime for the same reason.
    mkdir -p "$stage"/runtime
    cp runtime/tycho_rt.c "$stage"/runtime/
    cp README.md LICENSE "$stage"/
    mkdir -p "$stage"/examples
    cp examples/hello.ty "$stage"/examples/ 2>/dev/null || true

    echo ">> smoke-testing the packaged layout (corelib found beside the binary)"
    tmp="$(mktemp -d)"
    tar -C dist -cf - "$name" | tar -C "$tmp" -xf -
    printf 'package main\nimport "core:strings"\nfn main():\n    println(strings.to_upper("release ok"))\n' > "$tmp/t.ty"
    ( cd "$tmp/$name" && "./tychoc$EXE" "$tmp/t.ty" -o "$tmp/t" ) && "$tmp/t$EXE" | grep -q "RELEASE OK" \
        || { echo "!! packaged compiler failed its smoke test" >&2; rm -rf "$tmp"; exit 1; }
    rm -rf "$tmp"
fi

# Keep archive metadata independent of the build time. This spelling works with
# both BSD touch (macOS) and GNU touch (Linux).
find "$stage" -exec env TZ=UTC0 touch -t 202311142213.20 {} +

echo ">> compressing"
tar -C dist -cf - "$name" | gzip -n > "dist/${name}.tar.gz"
rm -rf "$stage"
( cd dist && { sha256sum "${name}.tar.gz" 2>/dev/null || shasum -a 256 "${name}.tar.gz"; } > "${name}.tar.gz.sha256" )

echo
echo "built: dist/${name}.tar.gz"
cat "dist/${name}.tar.gz.sha256"
echo
# 0.x is not a stable API, so GitHub must mark the release a pre-release or the
# tag reads as supported. v0.5.0 shipped without it and was edited afterwards.
prerel=""
case "$version" in v0.*) prerel=" --prerelease" ;; esac

echo "publish with:"
echo "  gh release create ${version} dist/${name}.tar.gz dist/${name}.tar.gz.sha256 --notes-file RELEASE_NOTES.md${prerel}"
