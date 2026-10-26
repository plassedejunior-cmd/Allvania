#!/bin/sh
# Downloads the melonDS DS libretro core for this Mac into cores/bin/ (copied next to ooe by the build).
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
case "$(uname -m)" in
	arm64) ARCH=arm64 ;;
	*) ARCH=x86_64 ;;
esac
URL="https://buildbot.libretro.com/nightly/apple/osx/$ARCH/latest/melondsds_libretro.dylib.zip"

mkdir -p "$ROOT/cores/download" "$ROOT/cores/bin"
curl -fL -o "$ROOT/cores/download/melondsds_libretro.dylib.zip" "$URL"
unzip -o -q "$ROOT/cores/download/melondsds_libretro.dylib.zip" -d "$ROOT/cores/download"
cp "$ROOT/cores/download/melondsds_libretro.dylib" "$ROOT/cores/bin/"
echo "core em $ROOT/cores/bin; recompile com cmake --build build-port para copiá-lo"
