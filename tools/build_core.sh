#!/bin/sh
# Builds the melonDS DS libretro core from its official source (a pinned release tag)
# and puts it in cores/bin/, which the port build copies next to ooe.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
TAG=v1.4.0
SRC="$ROOT/cores/melonds-ds"

if [ ! -d "$SRC/.git" ]; then
	git clone https://github.com/JesseTG/melonds-ds.git "$SRC"
fi
git -C "$SRC" fetch --tags --quiet
git -C "$SRC" checkout --quiet "$TAG"

cmake -S "$SRC" -B "$SRC/build" -DCMAKE_BUILD_TYPE=Release -DENABLE_OPENGL=OFF 
cmake --build "$SRC/build" -j

mkdir -p "$ROOT/cores/bin"
cp "$(find "$SRC/build" -maxdepth 3 -name 'melondsds_libretro.*' -not -name '*.info' | head -1)" "$ROOT/cores/bin/"
echo "núcleo em $ROOT/cores/bin; rode cmake --build build-port para copiá-lo ao lado do ooe"
