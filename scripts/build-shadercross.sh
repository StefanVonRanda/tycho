#!/bin/sh
set -eu
# Build the pinned SDL_shadercross runtime used by core:gfxgpu. It translates
# the committed SPIR-V shaders to the native format for Vulkan, Metal or D3D12.
# The install prefix is under build/ so a checkout stays self-contained.
cd "$(dirname "$0")/.." || exit 2
prefix="${1:-$PWD/build/shadercross}"
src="$PWD/build/shadercross-src"
mkdir -p build
if [ ! -d "$src/.git" ]; then
    git clone --recursive https://github.com/libsdl-org/SDL_shadercross.git "$src"
fi
git -C "$src" checkout 1ff05bec573988a98ef9e0260b4da44f512b8367
git -C "$src" submodule update --init --recursive
cmake -S "$src" -B "$src/build" \
    -DSDLSHADERCROSS_VENDORED=ON \
    -DSDLSHADERCROSS_DXC=OFF \
    -DSDLSHADERCROSS_SHARED=ON \
    -DSDLSHADERCROSS_STATIC=OFF \
    -DSDLSHADERCROSS_SPIRVCROSS_SHARED=ON \
    -DSDLSHADERCROSS_CLI=OFF \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$prefix"
cmake --build "$src/build" -j4
cmake --install "$src/build"
printf '\nSDL_shadercross installed in %s\n' "$prefix"
printf 'For a build:\n  export PKG_CONFIG_PATH="%s/lib64/pkgconfig:%s/lib/pkgconfig:$PKG_CONFIG_PATH"\n' "$prefix" "$prefix"
printf 'For Linux run:\n  export LD_LIBRARY_PATH="%s/lib64:%s/lib:$LD_LIBRARY_PATH"\n' "$prefix" "$prefix"
printf 'For macOS run:\n  export DYLD_LIBRARY_PATH="%s/lib:$DYLD_LIBRARY_PATH"\n' "$prefix"
