#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT="${1:-$HOME/PokemonDev/pokekyoto}"
BASE_PATCH="$SCRIPT_DIR/patches/01_sakura_autumn_base.patch"
DRAIN_PATCH="$SCRIPT_DIR/patches/02_no_dither_fast_exit.patch"
BRANCH="feature/sakura-weather-20261024"
TARGET_HEAD="4dcb7048ecaa400f11927f07c5528b726ddb5b1e"

if [[ ! -d "$PROJECT/.git" ]]; then
  echo "ERROR: $PROJECT is not a git repository."
  echo "Usage: $0 /path/to/pokekyoto"
  exit 1
fi

cd "$PROJECT"
echo "PokéKyoto project: $PWD"
echo "Current HEAD: $(git rev-parse HEAD)"

# Unrelated local map/art changes are allowed. git apply --check below protects
# every file touched by this weather update and refuses a conflicting install.
if git show-ref --verify --quiet "refs/heads/$BRANCH"; then
  if [[ "$(git branch --show-current)" != "$BRANCH" ]]; then
    echo "ERROR: branch $BRANCH already exists."
    echo "Switch to it manually if this is a previous installation attempt."
    exit 1
  fi
else
  git switch -c "$BRANCH"
fi

if git apply --reverse --check "$DRAIN_PATCH" >/dev/null 2>&1; then
  echo "Sakura weather already appears to be installed; nothing changed."
else
  echo "Checking Sakura/Autumn base patch..."
  git apply --check "$BASE_PATCH"
  echo "Applying Sakura/Autumn base patch..."
  git apply "$BASE_PATCH"

  echo "Checking no-dither + fast-map-exit update..."
  git apply --check "$DRAIN_PATCH"
  echo "Applying no-dither + fast-map-exit update..."
  git apply "$DRAIN_PATCH"
fi

echo "Checking whitespace/errors in patch result..."
git diff --check

echo "Building PokéKyoto..."
make -j"$(nproc)"

echo
echo "SUCCESS"
echo "- Sakura Petals = WEATHER_DOWNPOUR / WEATHER_SAKURA_PETALS (ID 13)"
echo "- Autumn Wind = WEATHER_AUTUMN_WIND (ID 20)"
echo "- No dither or alpha fade"
echo "- No new particles spawn while leaving"
echo "- Surviving particles speed up 1.6x only after the destination map loads"
echo
echo "For a map test, set its weather to Downpour/Sakura Petals in OpalMap or Porymap,"
echo "then rebuild if your editor changed source data."

echo
echo "Review with: git status && git diff --stat"
echo "Commit when satisfied."
