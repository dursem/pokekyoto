#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
SOURCE="${1:-$HOME/PokemonDev/porymap_v2/Kyoto_Emerald_Opal_Porymap_v2_Palettes/build/porymap-source}"
BUILD="$(dirname "$SOURCE")/porymap-build"
python3 "$ROOT/upgrade_triple_layer.py" --source "$SOURCE"
mkdir -p "$BUILD"
cd "$BUILD"
qmake6 "$SOURCE/porymap.pro"
make -j"$(nproc)"
echo
echo "Updated Porymap built at: $BUILD/porymap"
