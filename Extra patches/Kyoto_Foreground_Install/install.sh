#!/usr/bin/env bash
# Apply the tested foreground weather + Paraglider patch without replacing a repo.
set -euo pipefail
installer_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
fail() { printf '\nGESTOPT: %s\nEr is niets geforceerd of verwijderd. Stuur deze melding door voor hulp.\n' "$1" >&2; exit 1; }
command -v git >/dev/null || fail 'Git is niet beschikbaar. Open dit in Ubuntu/WSL.'
repo_dir="$(git -C "$installer_dir/.." rev-parse --show-toplevel 2>/dev/null)" || fail 'Zet de map Kyoto_Foreground_Install naast de Makefile in je pokekyoto-project.'
cd -- "$repo_dir"
[[ -f Makefile && -f src/overworld.c && -f include/constants/items.h ]] || fail 'Dit lijkt geen pokeemerald decomp-project.'
full_patch="$installer_dir/foreground-install.patch"
upgrade_patch="$installer_dir/from-previous-package.patch"
if git apply --reverse --check "$full_patch" >/dev/null 2>&1; then
    printf 'Deze foreground-versie is al geinstalleerd. Je kunt make -j4 uitvoeren.\n'
    exit 0
fi
git diff --quiet && git diff --cached --quiet || fail 'Er zijn nog niet-gecommitte wijzigingen. Sla die eerst op met Commit in GitHub Desktop; verwijder ze niet.'
patch_file=''
if git apply --index --check "$full_patch" >/dev/null 2>&1; then
    patch_file="$full_patch"
elif git apply --index --check "$upgrade_patch" >/dev/null 2>&1; then
    patch_file="$upgrade_patch"
fi
[[ -n "$patch_file" ]] || fail 'De code wijkt af van de ondersteunde versies. De patch is niet toegepast. Stuur een screenshot van GitHub Desktop (History).'
branch_name='feature/clouds-glide-foreground'
current_branch="$(git branch --show-current)"
if [[ "$current_branch" != "$branch_name" ]]; then
    n=2
    while git show-ref --verify --quiet "refs/heads/$branch_name"; do
        branch_name="feature/clouds-glide-foreground-$n"
        n=$((n + 1))
    done
    git switch -c "$branch_name"
fi
git apply --index "$patch_file"
# Keep this install package out of the user's source commit (local setting only).
exclude_path="$(git rev-parse --git-path info/exclude)"
mkdir -p -- "$(dirname -- "$exclude_path")"
if ! grep -qxF '/Kyoto_Foreground_Install/' "$exclude_path" 2>/dev/null; then
    printf '\n/Kyoto_Foreground_Install/\n' >> "$exclude_path"
fi
printf '\nGELUKT: foreground-wolken en Paraglider zijn geinstalleerd.\n'
printf 'Project: %s\nBranch: %s\n' "$repo_dir" "$(git branch --show-current)"
printf 'Volgende stap in deze projectmap: make -j4\n'
printf 'Bekijk daarna de wijzigingen in GitHub Desktop en commit/publish de branch.\n'
printf 'Zet in Porymap WEATHER_SUNNY_CLOUDS op de gewenste map.\n'
