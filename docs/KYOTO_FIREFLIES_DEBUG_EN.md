# Fireflies replacing Shade, and B + Start debug menu

`WEATHER_SHADE` (ID 11) now runs the PokePeach Firefly Shade effect. Existing
maps and coordinate events using Shade require no ID migration. In Porymap,
choose `WEATHER_SHADE`. The in-game debug weather picker labels it `FIREFLIES`.

Hold **B**, then press **Start** in the overworld. Start alone retains the normal
menu. Press B to return from debug submenus and close the debug menu. Use
**Utilities → Set weather → 11** to preview the fireflies on the current map.
The map header remains the permanent weather setting. To disable the debug
shortcut in a future release, set `DEBUG_OVERWORLD_MENU` to `FALSE` in
`include/config/debug.h`. Battle and summary debug options are unchanged.

## Source provenance

Requested upstream: https://github.com/PeachLimeProject/pokepeach/tree/master.
The GitHub connector and unauthenticated Git access returned 404/authentication
failure on 2026-10-09. This port therefore uses the user's earlier `pokepeach.zip`
decomp archive, supplied on 2026-09-30, rather than claiming to reproduce the
unavailable current master. Its `src/field_weather_firefly.c` and three assets
in `graphics/weather/` provide the particle art, light masks, ten start positions,
phase/flicker tables, drift, world anchoring and three-second weather fades.
PokePeach's debug config confirms B held plus a Start press; Kyoto's existing
pokeemerald-expansion debug menu is enabled with that same shortcut.

## Kyoto integration

- `src/field_weather.c` dispatches Shade to FireflyShade callbacks and removes
  the old shade gamma fade from map/palette reconstruction. Map and actor colors
  retain Kyoto's existing day/night palette path.
- The yellow firefly palette uses the blend-immune tag `0x9202`, compatible with
  Kyoto's weather and time-of-day palette logic. Normal screen fades still apply.
- The invisible 16×16 OBJ masks disable hardware darkening in the small circles;
  outside them, BG1–3 and objects receive brightness level 3. BG0 menu/message
  colors are excluded from this hardware darkening. Particle priority 1 leaves
  BG0 dialogue and menu windows in front.
- The shared alpha register is restored to Kyoto's normal lamp/shadow setting
  when the particle fade completes. Ground shadows are suspended during weather
  alpha fades and resume afterwards, avoiding opaque black shadows.
- Main-callback changes and `ResetSpriteData` release firefly sprites, tiles,
  palette and saved window/blend state before menu resources reuse their slots.
  Weather initialization recreates the effect when returning to the field.
- `FireflyShade_Prepare` reapplies the effect after the field frame is prepared.
  Iris transitions and hardware fades retain window ownership.
- Allocation failures skip particles rather than evicting other resources.

Cloud graphics, cloud silhouettes, paraglider behavior, Pokémon icons and the
existing lantern/actor lighting code are retained. Fireflies replace Shade;
they are not combined with the foreground-cloud weather on the same map.

## Installation

Copy the entire `Pokekyoto_Fireflies_Debug` folder into the Kyoto repository.
Commit existing source changes first, including the lantern installation and
the box bounds fix if already applied. Do not commit the installer folder.
In Ubuntu:

```bash
cd ~/PokemonDev/pokekyoto
bash Pokekyoto_Fireflies_Debug/install_fireflies_debug.sh
```

The installer checks compatibility, creates a new branch, applies only this
update and builds the ROM. It handles the latest lantern installation with or
without the subsequent box bounds fix. It does not force patches, commit, push,
alter save files or add test Pokémon/maps. Stop on an error and share its output.

`validation.json` in the ZIP records the actual mGBA checks and their limits.
The separate video is emulator footage from a test-only map setup. Temporary
boot shortcuts, test Pokémon and weather/warp hooks are excluded from the patch.
