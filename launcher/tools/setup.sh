#!/bin/sh
# One-shot build of everything on macOS: the DS core (from source), the Order of Ecclesia
# port and the launcher. Expects ooe-decomp/ next to launcher/.
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
brew install cmake sdl3 sdl3_ttf sdl3_image pkgconf
"$ROOT/ooe-decomp/tools/build_core.sh"
cmake -S "$ROOT/ooe-decomp" -B "$ROOT/ooe-decomp/build-port" -DCMAKE_BUILD_TYPE=Release
cmake --build "$ROOT/ooe-decomp/build-port" -j
cmake -S "$ROOT/launcher" -B "$ROOT/launcher/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$ROOT/launcher/build" -j
echo "Pronto: $ROOT/launcher/build/launcher"
