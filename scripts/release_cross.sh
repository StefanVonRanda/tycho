#!/bin/sh
# Cross-build the shipped artifacts for every supported target, using `zig cc` as
# the one cross driver. Adds macOS and ARM64 to the two targets scripts/release.sh
# already produced natively (host linux, and windows via mingw).
#
#   sh scripts/release_cross.sh v0.7.0            all six targets
#   sh scripts/release_cross.sh v0.7.0 <triple>   just one
#
# WHY zig cc AND NOT SIX TOOLCHAINS. zig ships the libcs and the linkers for all
# of these in one binary, so a Mac artifact needs no Mac and an ARM artifact needs
# no ARM board. That is also the limit of what it buys: it makes binaries, it does
# not run them.
#
# **EVERY ARTIFACT THIS PRODUCES IS UNTESTED EXCEPT THE HOST ONE.** There is no
# Darwin machine here, no ARM machine, no qemu and no working container runtime --
# all three checked 2026-08-15. `file(1)` says the right architecture and the
# compile is clean; nothing has executed. The stack-overflow guard in
# runtime/tycho_rt.c is per-architecture code (x86_64/i386/arm64 branches for
# Darwin, a separate __aarch64__ branch for Linux) and is exactly the shape that
# compiles everywhere and works in one place. Ship these labelled, or not at all.
#
# The tools are built the way scripts/release.sh's mingw leg builds them: the
# NATIVE tychoc emits their C here, and the cross driver compiles that C for the
# target. tychoc is a transpiler, so the emitted C is host-independent.
set -eu

cd "$(dirname "$0")/.."
root="$PWD"

version="${1:-}"
[ -n "$version" ] || { echo "usage: sh scripts/release_cross.sh <version> [target]" >&2; exit 2; }
only="${2:-}"

command -v zig >/dev/null 2>&1 || { echo "!! zig is not installed; it is the cross driver" >&2; exit 2; }

# target triple -> artifact name. The artifact name is what a user reads, so it
# uses the platform words people search for rather than zig's triple.
TARGETS="x86_64-linux-gnu:linux-x86_64
aarch64-linux-gnu:linux-arm64
x86_64-macos-none:macos-x86_64
aarch64-macos-none:macos-arm64
x86_64-windows-gnu:windows-x86_64
aarch64-windows-gnu:windows-arm64"

echo ">> building the native compiler first (it emits the tools' C)"
make -s tychoc

ver="$(./tychoc --version | awk '{print $2}')"
[ "v$ver" = "$version" ] || {
    echo "!! version mismatch: src/tychoc.c says $ver, asked for $version" >&2
    echo "   bump TYCHO_VERSION in src/tychoc.c and CHANGELOG.md together" >&2
    exit 2; }

built=0
for row in $TARGETS; do
    triple="${row%%:*}"; plat="${row##*:}"
    [ -z "$only" ] || [ "$only" = "$triple" ] || [ "$only" = "$plat" ] || continue

    case "$triple" in *windows*) exe=".exe" ;; *) exe="" ;; esac
    name="tycho-${version}-${plat}"
    stage="dist/$name"
    echo ">> $plat  ($triple)"
    rm -rf "$stage"; mkdir -p "$stage"

    zig cc -target "$triple" -O2 -fwrapv -std=c11 -Ibuild src/tychoc.c \
        -o "$stage/tychoc$exe" 2>"$root/dist-cross.log" || {
        echo "!! tychoc failed for $triple"; tail -5 "$root/dist-cross.log"; exit 1; }

    for spec in "tychofmt tools/tychofmt.ty -" \
                "tycho-lsp tools/lsp.ty tools/lsp_shim.c" \
                "tycho-debug tools/tycho-debug/main.ty tools/tycho-debug/debug_shim.c"; do
        set -- $spec
        tname="$1"; tentry="$2"; tshim="$3"
        # --shim, not a bare path: a bare argument is parsed as Tycho SOURCE and
        # the C shim dies on "char literal must be exactly one character".
        shimarg=""; [ "$tshim" != "-" ] && shimarg="--shim $tshim"
        # shellcheck disable=SC2086
        ./tychoc "$tentry" $shimarg --emit-c -o "$stage/$tname" >/dev/null || {
            echo "!! could not emit C for $tname"; exit 1; }
        # shellcheck disable=SC2086
        # --print-shims lists the CORELIB shims an import pulls in; it does not
        # echo back an explicit --shim, so that one is appended by hand -- the
        # same two-part link line scripts/release.sh's mingw leg uses.
        tshims="$(./tychoc "$tentry" $shimarg --print-shims | tr '\n' ' ')"
        [ "$tshim" != "-" ] && tshims="$tshims $tshim"
        zig cc -target "$triple" -O2 -fwrapv -o "$stage/$tname$exe" \
            "$stage/$tname.c" $tshims -lm 2>>"$root/dist-cross.log" || {
            echo "!! $tname failed for $triple"; tail -5 "$root/dist-cross.log"; exit 1; }
        rm -f "$stage/$tname.c"
    done

    cp -R corelib examples README.md LICENSE "$stage/"
    # A note IN the artifact, not only in the release page: whoever unpacks this
    # on a Mac should know nobody has run it there.
    case "$plat" in
        # linux-x86_64 is the host. windows-x86_64 is exercised under wine by
        # scripts/wine_*.sh and was re-checked for this build (reports its
        # version and emits C), so neither carries the notice.
        linux-x86_64|windows-x86_64) ;;
        *) printf 'This build was cross-compiled with `zig cc` on linux-x86_64 and has\nNOT been executed on %s. It compiles cleanly and reports the right\narchitecture; that is all that is known. Please report anything that\nbreaks -- see SECURITY.md and CONTRIBUTING.md in the repository.\n' \
             "$plat" > "$stage/UNTESTED-PLATFORM.txt" ;;
    esac

    ( cd dist && tar czf "$name.tar.gz" "$name" && sha256sum "$name.tar.gz" > "$name.tar.gz.sha256" )
    rm -rf "$stage"
    built=$((built + 1))
    printf '   %s  %s\n' "$(file -b "dist/$name.tar.gz" | cut -c1-24)" "dist/$name.tar.gz"
done

rm -f "$root/dist-cross.log"
[ "$built" -gt 0 ] || { echo "!! no target matched '$only'" >&2; exit 2; }
echo "release-cross: built $built artifact(s). linux-x86_64 is the host and windows-x86_64 runs under wine; the other four are UNTESTED and say so in UNTESTED-PLATFORM.txt"
