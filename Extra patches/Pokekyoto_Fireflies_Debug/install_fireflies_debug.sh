#!/usr/bin/env bash
set -euo pipefail
installer_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="${1:-$PWD}"
repo_dir="$(git -C "$repo_dir" rev-parse --show-toplevel)" || {
    echo 'Open Ubuntu and first run: cd ~/PokemonDev/pokekyoto' >&2
    exit 1
}
cd "$repo_dir"
if git apply --reverse --check "$installer_dir/from-lantern-box-fix.patch" >/dev/null 2>&1; then
    echo 'The fireflies and B + Start debug menu are already installed.'
    exit 0
fi
if ! git diff --quiet || ! git diff --cached --quiet; then
    echo 'Commit your existing changes first, including the lantern installation and box bounds fix.' >&2
    echo 'No update was applied. You do not need to push.' >&2
    exit 1
fi
chosen_patch=''
for candidate in from-lantern.patch from-lantern-box-fix.patch; do
    if git apply --index --check "$installer_dir/$candidate" >/dev/null 2>&1; then
        chosen_patch="$installer_dir/$candidate"
        break
    fi
done
if [[ -z "$chosen_patch" ]]; then
    echo 'This installer expects the latest Kyoto lantern update. Your source differs; no patch was applied.' >&2
    echo 'Keep your files and share the output below; do not force the update.' >&2
    git apply --index --check "$installer_dir/from-lantern-box-fix.patch" || true
    exit 1
fi
branch_name='feature/fireflies-debug'
if git show-ref --verify --quiet "refs/heads/$branch_name"; then
    branch_name="feature/fireflies-debug-$(date +%Y%m%d-%H%M%S)"
fi
git switch -c "$branch_name"
git apply --index "$chosen_patch"
exclude_file="$(git rev-parse --git-path info/exclude)"
if ! grep -qxF '/Pokekyoto_Fireflies_Debug/' "$exclude_file" 2>/dev/null; then
    echo '/Pokekyoto_Fireflies_Debug/' >> "$exclude_file"
fi
if [[ "${KYOTO_SKIP_BUILD:-0}" != 1 ]]; then
    if ! make -j4; then
        echo 'Source applied on the new branch, but the build failed. Keep this branch and share the last error lines.' >&2
        exit 1
    fi
fi
printf '\nInstalled on branch: %s\n' "$branch_name"
echo 'Open pokeemerald.gba in mGBA. Hold B and press Start to open debug.'
echo 'Select WEATHER_SHADE in Porymap for fireflies, or Utilities > Set weather > 11 in debug.'
echo 'Review and commit the staged source changes. No commit or push was made automatically.'
