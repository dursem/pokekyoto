#!/usr/bin/env bash
set -euo pipefail
ROOT="${1:-$HOME/PokemonDev/pokekyoto}"
cd "$ROOT"
branch="$(git branch --show-current)"
if [[ "$branch" != "kyoto-opal-tileset-expansion" ]]; then
  echo "Refusing: expected branch kyoto-opal-tileset-expansion, found $branch" >&2
  exit 1
fi
if [[ -n "$(git status --porcelain)" ]]; then
  echo "Refusing: working tree is not clean. Commit/push the current checkpoint first." >&2
  exit 1
fi
python3 "$(dirname "$0")/project_tools/install_palette_authoring.py" "$ROOT"
echo
echo "Authoring files installed. Review with: git status && git diff -- data/tilesets/opal_palettes.json tools/opal_map_palettes/compiler.py"
