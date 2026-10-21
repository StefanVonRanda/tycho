#!/bin/sh
set -eu
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
LD_LIBRARY_PATH="/usr/local/lib64:/usr/local/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LD_LIBRARY_PATH

# The packaged GPU example carries the shadercross runtime beside the binary.
# SDL3 itself remains a host dependency.
RPATH="-Wl,-rpath,\$ORIGIN"
case "$(uname -s)" in
  Darwin) RPATH="-Wl,-rpath,@loader_path" ;;
esac
TYCHO_CFLAGS="${TYCHO_CFLAGS:-} $RPATH"
export TYCHO_CFLAGS

rm -rf dist
mkdir -p dist
"$TYCHOC" main.ty --pkg sdl3 -o dist/vector-vault.bin
cp -R assets dist/assets
cp README.md dist/README.md
cat > dist/vector-vault <<'RUN'
#!/bin/sh
cd "$(dirname "$0")" || exit 2
LD_LIBRARY_PATH="$PWD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
DYLD_LIBRARY_PATH="$PWD${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
export LD_LIBRARY_PATH DYLD_LIBRARY_PATH
exec ./vector-vault.bin "$@"
RUN
chmod +x dist/vector-vault
for d in lib lib64; do
    [ -d "$PREFIX/$d" ] || continue
    for f in "$PREFIX/$d"/libSDL3_shadercross*.so* \
             "$PREFIX/$d"/libSDL3_shadercross*.dylib \
             "$PREFIX/$d"/libspirv-cross-c-shared*.so* \
             "$PREFIX/$d"/libspirv-cross-c-shared*.dylib; do
        [ -e "$f" ] && cp "$f" dist/
    done
done
printf 'Built dist/vector-vault with its shadercross runtime; run it from dist/.\n'
