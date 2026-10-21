#!/bin/sh
set -eu
cd "$(dirname "$0")" || exit 2
TYCHOC="${TYCHOC:-../../../tychoc1}"
[ -x "$TYCHOC" ] || { echo "no $TYCHOC — run make at the repo root"; exit 2; }

rm -rf dist
mkdir -p dist
"$TYCHOC" main.ty --pkg sdl3 -o dist/vector-vault
cp -R assets dist/assets
cp README.md dist/README.md
printf 'Built dist/vector-vault; run it from dist/ so assets/font.png resolves.\n'
