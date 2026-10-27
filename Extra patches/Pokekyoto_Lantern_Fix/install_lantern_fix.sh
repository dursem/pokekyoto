#!/usr/bin/env bash
set -euo pipefail
installer_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="${1:-$PWD}"
repo_dir="$(git -C "$repo_dir" rev-parse --show-toplevel)" || {
    echo 'Open Ubuntu and first run: cd ~/PokemonDev/pokekyoto' >&2
    exit 1
}
cd "$repo_dir"
if git apply --reverse --check "$installer_dir/from-light-menu.patch" >/dev/null 2>&1; then
    echo 'The lantern-cap correction is already installed.'
    exit 0
fi
if ! git diff --quiet || ! git diff --cached --quiet; then
    echo 'Your project has uncommitted tracked changes.' >&2
    echo 'First commit your existing work in GitHub Desktop (including the earlier cloud/glide installation), then run this installer again. You do not need to push.' >&2
    exit 1
fi
chosen_patch=''
for candidate in from-light-menu.patch from-native-icons.patch from-cloud-icons.patch from-lighting.patch from-foreground.patch from-master.patch; do
    if git apply --index --check "$installer_dir/$candidate" >/dev/null 2>&1; then
        chosen_patch="$installer_dir/$candidate"
        break
    fi
done
if [[ -z "$chosen_patch" ]]; then
    echo 'This project differs from the supported Kyoto versions. No patch was applied.' >&2
    echo 'Keep your files and send the output below to your developer; do not force the patch.' >&2
    git apply --index --check "$installer_dir/from-foreground.patch" || true
    exit 1
fi
branch_name='feature/lantern-fix'
if git show-ref --verify --quiet "refs/heads/$branch_name"; then
    branch_name="feature/lantern-fix-$(date +%Y%m%d-%H%M%S)"
fi
git switch -c "$branch_name"
git apply --index "$chosen_patch"
# Keep this installer package out of the subsequent source commit.
exclude_file="$(git rev-parse --git-path info/exclude)"
if ! grep -qxF '/Pokekyoto_Lantern_Fix/' "$exclude_file" 2>/dev/null; then
    echo '/Pokekyoto_Lantern_Fix/' >> "$exclude_file"
fi
if [[ "${KYOTO_SKIP_BUILD:-0}" != 1 ]]; then
    if ! make -j4; then
        echo 'Source update applied on the new branch, but the build failed. Keep this branch and send the last error lines; do not rerun or force another patch.' >&2
        exit 1
    fi
fi
printf '\nInstalled on branch: %s\n' "$branch_name"
echo 'Review and commit the staged source changes in GitHub Desktop.'
echo 'Suggested summary: Illuminate lantern caps while preserving actors and menus'
echo 'No commit or push was made automatically.'
