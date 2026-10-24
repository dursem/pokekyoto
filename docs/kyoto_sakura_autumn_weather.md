# PokéKyoto: Sakura petals and Autumn Wind

This update targets `dursem/pokekyoto` Emerald source at commit
`4dcb7048ecaa400f11927f07c5528b726ddb5b1e` (23 October 2026).

## Weather selection

| Effect | Map/script constant | ID | Debug-menu label |
| --- | --- | --- | --- |
| Small pink Sakura petals | `WEATHER_DOWNPOUR` or `WEATHER_SAKURA_PETALS` | 13 | SAKURA PETALS |
| Red/gold Autumn Wind | `WEATHER_AUTUMN_WIND` | 20 | AUTUMN WIND |

Downpour's existing ID is retained. Maps already configured with Downpour now
show the small Sakura petals. Autumn Wind is appended after the existing
weather IDs, so existing map headers and saved weather numbers are not shifted.

The existing debug menu is opened by holding **B** and pressing **START**.
Choose **Utilities → Set weather…**, then select ID **13** or **20**.
The debug selector includes the new weather.

The old ordered-dither fade has been removed. When leaving Sakura or Autumn
Wind, the effect immediately stops recycling particles: no new petals/leaves
are spawned and no existing particle is wrapped back onto the opposite edge.
Every particle keeps its normal movement, sway/pose animation and full artwork
until it naturally crosses its normal wrap boundary, where it is removed.

On the source map this remains a natural drain at the exact normal particle
speed. The renderer remembers the last map on which the weather was fully
active. Once `gSaveBlock1Ptr->location.mapGroup/mapNum` changes to the connected
destination, only the surviving outgoing particles receive a 1.6x motion boost.
No speed-up happens before the boundary. From the same full stationary test
population, Sakura's final retirement changes from about 17.46 s at normal speed
to about 11.12 s with the destination-map boost (average about 6.03 s). Autumn
Wind changes from about 4.44 s to about 3.01 s. Real transitions are usually
shorter because some particles can retire before the map ID changes.

The destination weather begins after the outgoing population has fully drained.
Same-weather connected maps keep the existing particles without restarting them
and therefore do not enter fast-drain mode. If the player walks back during a
drain, surviving particles are kept and only the missing density is allowed to
re-enter from the normal offscreen spawn edge after the cancellation.

To set a permanent map weather in OpalMap/Porymap, set the map header's weather
to `WEATHER_DOWNPOUR` for Sakura or `WEATHER_AUTUMN_WIND` for Autumn Wind.
Reload the editor project after applying the source update so it reads the new
weather constant. No map layout or tileset needs to be replaced.

Script examples:

```asm
    setweather WEATHER_SAKURA_PETALS
    doweather
```

```asm
    setweather WEATHER_AUTUMN_WIND
    doweather
```

Optional coordinate weather events have their own independent numbering:
`COORD_EVENT_WEATHER_SAKURA_PETALS` is 22 and
`COORD_EVENT_WEATHER_AUTUMN_WIND` is 23. These are coordinate-event IDs, not
map-header weather IDs.

## Sakura appearance and exact tree-color matching

The renderer reuses the previously approved small petal artwork: three visible
colors, transparency, and nine tilt poses in fixed 16×16 sprite cells. The art
inside those cells remains small. Up to 24 petals drift downward slowly with
individual side-to-side motion. There is no affine scaling or constant diagonal
rain motion.

The color source is the pink tree in the current General primary tileset:

| Petal color | General logical palette | Tree color index | Authored RGB |
| --- | --- | --- | --- |
| Light pink | 3 | 9 | 255, 189, 181 |
| Middle pink | 3 | 10 | 239, 156, 173 |
| Dark pink | 3 | 11 | 214, 123, 148 |

These RGB values describe the source `.pal` file. The game stores its normal
15-bit GBA representation. Petal colors are read from the actual compiled tree
palette, not duplicated as unrelated hard-coded pink RGB values.

The current map engine can move a logical palette to a different physical BG
bank while scrolling. `MapPalettesGetPhysicalBank()` exposes the current bank
without allocating or pinning another map palette. Immediately before palette
DMA, `Petals_BeforePaletteTransfer()` copies the tree's three final displayed
colors into the dedicated petal OBJ palette. Day/night interpolation, weather
coloring, palette-bank changes and software screen fades have already been
applied at that point. The colors are not tinted a second time.

If the tree palette is not currently resident, the ordinary weather/day/night
OBJ pipeline uses the same original tree colors as its baseline. This avoids
reserving an extra background palette. The current General tileset has no
alternate night palette for these pink entries; its emissive window-color
overrides affect other entries.

`include/kyoto_petals_config.h` contains the palette/index references. Editing
the tree's three colors in `data/tilesets/primary/general/palettes/03.pal`
automatically changes the petals when rebuilding. If the tree is moved to a
different logical palette or different color indices, update the references
in that config header too.

## Autumn Wind

Autumn Wind reuses the original red and gold leaf graphics from the previous
Autumn Wind source package. Eight leaves travel from right to left, with a
gentle vertical wave, rotating poses and a slow variation in gust strength.
Each leaf type has eight fixed 16×16 poses.

The autumn palette goes through the game's normal day/night and screen-fade
processing. The exact pink-tree matching hook applies only to Sakura.

## Menus, transitions and rendering

Both effects use ordinary 4bpp OBJ sprites at priority 1. BG0 text boxes and
menus draw in front. The particles do not alter shared hardware alpha settings
to control their opacity, and they do not consume the gameplay RNG stream.

There is no per-pixel coverage mask and no custom opacity ramp. The packed
sprite sheets stay byte-identical for the whole lifetime of the weather. The
transition also never writes a new global `BLDALPHA` value, because that register
is shared with PokéKyoto's semi-transparent lamp/shadow effects. This avoids the
filter/dither look and prevents weather transitions from changing lighting.

When leaving the overworld or resetting the sprite pool, the effect snapshots
its exact surviving particle data, drain state, new-map speed state, drain-origin map and pending destination weather,
then releases only its own sprites, graphics and palette. Returning at the same
map/player position restores only those survivors; it does not refill particles
while the outgoing transition is still active. `ResumePausedWeather()` therefore
continues an interrupted drain after Bag/Party instead of restarting a full
weather field. Resources are released after the final survivor leaves. Allocation
failure does not hang the weather initializer.

The resource tag is `0x1210` in the separate graphics and palette namespaces.
Sakura uses 36 8×8 tiles (1,152 bytes); Autumn Wind uses 64 8×8 tiles (2,048
bytes). Only the currently active effect's sheet is loaded. Each uses one OBJ
palette, with no additional permanently reserved BG palette.

## Rain behavior

Weather 13 is now decorative Sakura. It no longer starts rain sprites, thunder,
rain sounds, a rain battle condition, rain-based evolution, Water-type Weather
Ball previews or Rainy Castform follower forms. Autumn Wind is also decorative.
Ordinary Rain and Rain Thunderstorm retain their weather behavior.

The existing Groudon/Kyogre storm scripts and alternating abnormal-weather
sequence use the existing Rain Thunderstorm slot instead of the repurposed
Downpour slot, so those rain sequences remain storms.

## Build and install

Use the existing PokéKyoto source folder. The update does not require a second
project, a base-ROM replacement, or reimporting map/tileset files.

Choose one patch according to what is already installed. For a first
installation of Sakura and Autumn Wind, enter the existing repository and run:

```sh
git apply --check /path/to/pokekyoto-sakura-autumn.patch
git apply /path/to/pokekyoto-sakura-autumn.patch
make -j8
```

If the previous Sakura/Autumn smooth-fade update from this conversation is
already installed, apply the no-dither drain patch on top:

```sh
git apply --check /path/to/pokekyoto-no-dither-drain.patch
git apply /path/to/pokekyoto-no-dither-drain.patch
make -j8
```

If starting from the target commit, first apply `pokekyoto-sakura-autumn.patch`
and then apply `pokekyoto-no-dither-drain.patch`. The second patch deliberately
replaces the earlier ordered-dither renderer; do not apply the old
`pokekyoto-smooth-fades-only.patch` after it.

Use the actual extracted patch path. If the check reports a conflict, stop and
merge the indicated source changes with the newer local work. Do not force an
old whole-project ZIP over a newer project. The patch includes the new source,
headers and packed graphics.

The packed `.4bpp` files are deliberate source assets: their frames are stored
as four contiguous tiles per 16×16 pose. The included `*_reference.png` sheets
are visual references; they are named separately to prevent a generic
row-major PNG conversion from silently repacking the frames incorrectly.

## Focused verification

`test/field_weather_petals.c` contains integration checks for exact tree-color
matching under time blending, screen fades and physical-bank relocation;
offscreen-tree/menu guards; resource cleanup/recreation; and unchanged weather
IDs and normal-rain battle behavior.

It also checks that the outgoing population can only decrease, that sprite-sheet
pixels remain byte-identical during the drain, that shared hardware blend
coefficients do not change, that a Bag/Party interruption restores only the
surviving population, and that route-back cancellation keeps survivors before
allowing missing density to re-enter from offscreen.

With the project's test tools installed, the focused suite can be run using:

```sh
make -j8 check \
  TEST_SRCS='test/test_runner.c test/test_runner_args.c test/test_runner_battle.c test/test_test_runner.c test/field_weather_petals.c' \
  TESTS='Petal weather'
```

See the package's validation report for the exact compiler, emulator version,
build result and runtime checks recorded for the delivered build.
