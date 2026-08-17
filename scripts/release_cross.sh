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
