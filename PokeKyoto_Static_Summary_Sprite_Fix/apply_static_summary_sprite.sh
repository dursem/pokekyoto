#!/usr/bin/env bash
set -euo pipefail

REPO="${1:-$HOME/PokemonDev/pokekyoto}"
SUMMARY="$REPO/src/pokemon_summary_screen.c"
ICON="$REPO/src/pokemon_icon.c"

if [[ ! -f "$SUMMARY" ]]; then
  echo "ERROR: Not found: $SUMMARY"
  exit 1
fi

if [[ ! -f "$ICON" ]]; then
  echo "ERROR: Not found: $ICON"
  exit 1
fi

cp -n "$SUMMARY" "$SUMMARY.before_static_summary_fix.bak" || true
cp -n "$ICON" "$ICON.before_static_summary_fix.bak" || true

python3 - "$SUMMARY" "$ICON" <<'PY'
from pathlib import Path
import re
import sys

summary_path = Path(sys.argv[1])
icon_path = Path(sys.argv[2])

summary = summary_path.read_text(encoding="utf-8")

pattern_summary = re.compile(
    r"static void SpriteCB_Pokemon\(struct Sprite \*sprite\)\s*\{.*?\}(?=\s*// Track and then destroy Task_PokemonSummaryAnimateAfterDelay)",
    re.S,
)

replacement_summary = '''static void SpriteCB_Pokemon(struct Sprite *sprite)
{
    // PokeKyoto: keep the large Pokemon portrait on the Summary/Info screen static.
    // The cry still plays once, but PokemonSummaryDoMonAnimation is deliberately skipped.
    if (!gPaletteFade.active)
    {
        PlayMonCry();
        sprite->animPaused = TRUE;
        sprite->data[2] = 1;
        sprite->callback = SpriteCallbackDummy;
    }
}
'''

summary_new, count = pattern_summary.subn(replacement_summary, summary, count=1)
if count != 1:
    raise SystemExit(
        "ERROR: Could not uniquely find SpriteCB_Pokemon in pokemon_summary_screen.c. "
        "No files were written."
    )

icon = icon_path.read_text(encoding="utf-8")
pattern_icon = re.compile(
    r"void SpriteCB_MonIcon\(struct Sprite \*sprite\)\s*\{.*?\}(?=\s*const u8 \*GetMonIconTiles)",
    re.S,
)

replacement_icon = '''void SpriteCB_MonIcon(struct Sprite *sprite)
{
    UpdateMonIconFrame(sprite);
}
'''

icon_new, icon_count = pattern_icon.subn(replacement_icon, icon, count=1)
if icon_count != 1:
    raise SystemExit(
        "ERROR: Could not uniquely find SpriteCB_MonIcon in pokemon_icon.c. "
        "No files were written."
    )

summary_path.write_text(summary_new, encoding="utf-8")
icon_path.write_text(icon_new, encoding="utf-8")

print("OK: large Summary/Info Pokemon portrait is now static.")
print("OK: 32x32 Pokemon icons are animated again.")
print()
print("Changed:")
print(" ", summary_path)
print(" ", icon_path)
PY

echo
echo "Verification:"
grep -n -A12 -B2 "static void SpriteCB_Pokemon" "$SUMMARY" | head -n 18
echo
grep -n -A4 -B1 "void SpriteCB_MonIcon" "$ICON" | head -n 8
echo
echo "Now build with:"
echo "  cd \"$REPO\""
echo "  make -j\$(nproc)"
