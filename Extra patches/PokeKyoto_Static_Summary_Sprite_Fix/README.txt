PokeKyoto - Static large Summary sprite / animated small icons

What this changes
=================
1. src/pokemon_summary_screen.c
   The large 64x64 Pokemon portrait shown on the Pokemon Info / Summary screen
   no longer runs PokemonSummaryDoMonAnimation().
   The Pokemon cry still plays once when the portrait appears.

2. src/pokemon_icon.c
   Restores SpriteCB_MonIcon() to UpdateMonIconFrame(sprite), so the small
   32x32 Pokemon icons are allowed to animate normally again.

This intentionally does NOT disable icon animation.

How to apply
============
From Ubuntu/WSL:

  cd ~/PokemonDev/pokekyoto
  bash /path/to/apply_static_summary_sprite.sh

Or give the repo path explicitly:

  bash /path/to/apply_static_summary_sprite.sh ~/PokemonDev/pokekyoto

Then build:

  cd ~/PokemonDev/pokekyoto
  make -j$(nproc)

Backups
=======
The script creates these once:
  src/pokemon_summary_screen.c.before_static_summary_fix.bak
  src/pokemon_icon.c.before_static_summary_fix.bak

Technical cause
===============
The large portrait is created in src/pokemon_summary_screen.c.
Its SpriteCB_Pokemon callback calls:

  PlayMonCry();
  PokemonSummaryDoMonAnimation(...);

The fix keeps PlayMonCry(), skips PokemonSummaryDoMonAnimation(), pauses the
sprite, and swaps its callback to SpriteCallbackDummy.

The small Pokemon icons use SpriteCB_MonIcon in src/pokemon_icon.c.
That callback is restored to:

  UpdateMonIconFrame(sprite);

so those icons keep their normal two-frame animation.
