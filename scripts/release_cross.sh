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
    echo "   bump TYCHO_VERSION in src/tychoc.c and README.md together" >&2
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
    # MEASURED, not asserted. This was a case statement naming linux-x86_64 and
    # windows-x86_64 as the tested pair, which made the shipped disclaimer a
    # hardcoded belief: aarch64-linux went 1065/1065 by hand and still shipped
    # saying nobody had run it, and a platform that REGRESSED would have gone on
    # shipping with no disclaimer at all. scripts/platform_matrix.sh executes
    # each platform and records a verdict; a PASS there is the only thing that
    # removes this file.
    matrix="$root/build/platform-matrix.tsv"
    verdict=""
    [ -f "$matrix" ] && verdict=$(awk -F'\t' -v p="$plat" '$1==p{print $2; exit}' "$matrix")
    case "$verdict" in
        PASS) ;;
        *)
            when="never"
            [ -f "$matrix" ] || when="no matrix has been run (scripts/platform_matrix.sh)"
            printf 'This build was cross-compiled with `zig cc` on linux-x86_64.\n\nIt has NOT been executed on %s: %s. It compiles cleanly and\nreports the right architecture; that is all that is known.\n\nWhat would change this line: a PASS for %s in\nbuild/platform-matrix.tsv, written by running\n`make platform-check` on a machine that can reach that platform.\n\nPlease report anything that breaks.\n' \
              "$plat" "${verdict:-$when}" "$plat" > "$stage/UNTESTED-PLATFORM.txt" ;;
    esac

    ( cd dist && tar czf "$name.tar.gz" "$name" && sha256sum "$name.tar.gz" > "$name.tar.gz.sha256" )
    rm -rf "$stage"
    built=$((built + 1))
    printf '   %s  %s\n' "$(file -b "dist/$name.tar.gz" | cut -c1-24)" "dist/$name.tar.gz"
done

rm -f "$root/dist-cross.log"
[ "$built" -gt 0 ] || { echo "!! no target matched '$only'" >&2; exit 2; }
tested=$(ls dist/*/UNTESTED-PLATFORM.txt 2>/dev/null | wc -l | tr -d ' ')
echo "release-cross: built $built artifact(s). Which of them have actually been"
echo "               EXECUTED is read from build/platform-matrix.tsv, not assumed;"
echo "               every platform without a PASS there carries UNTESTED-PLATFORM.txt."
[ -f "$root/build/platform-matrix.tsv" ] || \
  echo "               NOTE: no matrix on this machine -- run \`make platform-check\`, so" >&2
[ -f "$root/build/platform-matrix.tsv" ] || \
  echo "               every archive is marked untested even where coverage exists." >&2
