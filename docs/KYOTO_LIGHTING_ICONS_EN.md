# Kyoto local lighting, menu transitions and native-size PokéSprite icons

This corrected package targets dursem/pokekyoto. It can update the native-size icon package (ce185ed5), the previous cloud/icons correction, the earlier lighting/icons package, the foreground-clouds/paraglider package, or the original supported master checkout. Do not merge Merrp's followers branch into this project.

## Corrections in this version

- Removes the 24px icon downscaling. All 2,126 regular/shiny assets preserve their source pixel geometry and transparency masks. Charizard is now 32x27 visible pixels; designs up to 40x30 retain their complete wings and tails.
- Uses two hardware OBJ pieces (32x32 + 8x32) for a 40x32 transparent canvas. PC OAM capacity, tile reservations, popup ownership, bulk-selection redraws and release animation are adapted to these larger assets. The box grid remains six columns by five rows; large source designs can overlap neighbouring cells at their original size. Icons are shifted down four pixels to clear the title.
- Keeps icon type/shiny metadata separate from scrolling and party-animation scratch fields, fixing tile-cache leaks when changing boxes repeatedly.
- Restores the original brighter lamp palette while retaining steady, non-alternating colors. On the tested clear-weather Rustboro map, lamp alpha retains 12/16 of the underlying background. No emulator interframe filter is needed. Weather still owns the shared GBA alpha coefficients, so lamp strength can differ under other weather; this patch does not replace cloud blending with lamp blending.

## Local actor lighting and menu fixes in this revision

- The original `light.pal` brightness is restored. Ball lamps render at priority 1, subpriority 255; the lantern-cap correction below preserves visible actor pixels. Their existing alpha remains controlled by weather; no global blending changes are made.
- `kyoto_lighting.c` samples the existing 32x32 ball-light texture at five actor positions, with double weight at the center. Nearby lamps add together, clamped to 256. Strength ramps by at most 12/256 per rendered field frame. An illuminated actor moves from its current night/weather colors toward its own daytime palette with a warm cast. This is a whole-sprite palette tint based on overlap, not a per-pixel lighting mask. Sign lights are not part of the actor-light sampler.
- Each lit actor receives a private OBJ palette, tagged `0xD9C0 + objectId`. Only the matching rendered OAM entries are redirected; the object event's original palette reference and shared colors stay intact. Two NPCs sharing a native palette can therefore have different lighting. The player gets allocation priority. If no palette bank is available, the actor keeps its valid native palette; nothing is evicted. Banks are released outside the light, on callback changes and on sprite reset.
- Lighting runs after field OAM/fade updates and before the cloud silhouette compositor. Normal and time-of-day screen fades are applied to the target colors. The existing atomic video-transfer gate publishes the palette and rendered OAM together.
- Menu returns previously displayed an unfinished map for about eleven captured frames before fading in: tilemaps were visible while the camera, actors and Start menu were still being reconstructed. The field-loading palette guard now covers all maps, including maps without cloud weather. It remains black until the overworld callback is ready, so Pokédex, Party, Bag, Trainer Card and Options share the corrected return path.
- The male Bag resource contained magenta/blue alternating stripe colors. Entries 12/13 and 28/29 in `graphics/bag/menu_male.pal` now use a coherent light/dark blue pair. The female Bag's intentional pink palette is unchanged. This correction is independent of the field palette guard.

## Lantern-cap correction

The preceding local-lighting update placed ball glows at priority 2, which left the lantern artwork on BG1 dark. Ball lights now render at priority 1 again. `kyoto_lamp_mask.c` removes glow pixels only where a normal 4bpp object pixel is visible, allowing the original character and its private illuminated palette to show through. It checks BG1/BG2 occlusion, so a lantern cap or wall in front of the actor keeps its light and proper depth. Actor priorities and map metatiles are unchanged.

Each visible ball glow uses fixed 32x32 art without a shared affine transform. A private 512-byte OBJ sheet is allocated only when a cutout is needed and cached until field cleanup. The source bitmap and palette are unchanged. Up to sixteen CPU masks reserve 8,192 bytes of EWRAM, plus 128 bytes of metadata. VBlank uploads the used masks behind the existing complete-frame transfer gate. Tagged sheets are released when leaving the field or resetting sprites. If private OBJ tile allocation fails, that glow is hidden for the frame so it cannot erase an actor.

The mask reads queued sprite frames and queued map-tile graphics as well as the current camera offset. `OpalMapPalettesGetTileGraphics()` exposes pending tile bytes for that purpose. This avoids masking against the previous camera/animation frame. No new palettes are reserved. Empty mask slots skip tag searches, preserving the prior cloud-weather update rate.

mGBA checks for this correction: both lantern caps are visibly brighter; all 214 opaque pixels of the resting player match the preceding actor-lighting build; daytime lantern pixels are identical; the player and a nearby NPC retain independent local lighting; the five night-menu returns remain clean; and Bag cleanup releases actor palettes. At rest, the night fixture updates on all 600 sampled frames, while the cloud fixture advances on 546/600 frames (comparable to the previous 549/600).

## Preserved cloud corrections

- Restores the approved foreground cloud graphics, palette, three designs, movement and player/NPC silhouette compositor. Cloud alpha is again `(14, 2)`, with `(0, 16)` fade endpoints. The earlier `(8, 12)` change was incorrect because it changed the cloud appearance.
- Preserves transparent ground shadows independently. During ordinary cloud weather, shadow sprites become OBJ-window masks and BG1–3 are darkened by `OW_SHADOW_INTENSITY` (4/16). Semi-transparent cloud sprites continue using their original alpha. The underlying ground retains its texture and color.
- The window registers are overridden only during VBlank; the register manager's saved values remain intact. Menus, non-cloud maps, hardware fades and special iris/flash windows recover their own settings. BG0 UI and normal character sprites are excluded from shadow darkening.
- Keeps the cloud-to-noncloud black-screen-latch fix and the atomic palette/tilemap transfers from the previous package.
- Replaces nearest-front-palette recoloring with actual artwork from the user-requested `msikma/pokesprite/icons/pokemon/regular` directory and its matching `shiny` directory.

## Icon artwork and integration

Source: https://github.com/msikma/pokesprite/tree/master/icons/pokemon/regular

The import includes 1,068 species/form mappings and 1,063 designs, each with regular and shiny artwork. Every base species from Gen 1–7 is covered. Supported female and alternate forms are mapped separately. The exact upstream commit, source hashes, mapping, native bounds and palette-reduction exceptions are recorded in `graphics/pokemon/kyoto_pokesprite/manifest.json`.

The source images are 40×30 canvases. Transparent margins are removed, both variants use the same bounds, and their pixels are copied without scaling onto a 40×32 canvas. No opaque pixel is cropped. Colors are converted to GBA RGB555; only images exceeding 15 opaque hardware colors are reduced. No front-sprite palette is substituted. Upstream provides one pose, so the two-frame Emerald icon API repeats that pose. Native fallback icons retain both original poses.

Binary format: one 1,344-byte record per design, with a zero record at index 0. Regular and shiny each occupy 672 bytes: 640 tile bytes (16 tiles for the 32×32 piece followed by 4 tiles for the 8×32 piece), then 16 little-endian RGB555 palette entries. Palette index 0 is transparent. Do not replace `icons.bin` without the corresponding importer and renderer update. Generic untagged native icon allocations also reserve 640 bytes so they can be converted safely after creation.

Normal/shiny source tiles and palettes are selected using expansion's real `MON_DATA_IS_SHINY` field. Party, PC, Trade, contest results and the level-up banner use the new icon system. Thirty PC palettes are streamed across HBlanks. The held icon has its own palette, independent of the large front portrait. It is temporarily hidden while the marking dialog borrows both remaining UI palette slots, then restored.

The PC bulk-selection overlay uses the same imported normal/shiny artwork with a shared 96-color BG palette and the existing selection tint. After placement the exact individual icon palettes return. Generic species-only displays such as Dex lists, trainer-card stamps and old mail still use Kyoto's original shared-palette icons. Species/forms absent from this legacy source, including most Gen 8–9 additions and custom species, retain their original Kyoto icon and palette rather than being incorrectly recolored. Eggs retain native egg art and do not expose shininess.

PNG exports, binary assets and generated lookup tables are included. To regenerate using an upstream checkout (Pillow required only for regeneration):

```sh
python3 tools/import_kyoto_pokesprite.py /path/to/pokesprite
make -j4
```

For another menu that owns a real Pokémon, after creating an untagged icon:

```c
#include "kyoto_icons.h"
KyotoIconApplyMon(&gSprites[spriteId], mon);
```

The helper releases its tile/palette allocations on destruction, sprite reset and heap reset. If a menu exhausts OBJ palettes, it keeps the original valid icon rather than evicting a live UI palette.

## Retained lighting and game configuration

Kyoto's existing RTC and day/night tinting remain: `OW_ENABLE_DNS` is enabled and `OW_USE_FAKE_RTC` is unchanged. Followers remain disabled. This package does not introduce a second clock or change save layouts.

The previous Rustboro lighting additions remain: 17 lamp events and emissive window metatiles 58, 66, 88, 92, 93 and 120–124. Four additional logical palettes preserve the original tile colors while `.pla` flags mark glass indices 9 and 10. The Opal compiler preserves those flags and checks the 13-bank physical BG limit. Lamps use the original brighter palette, without alternating frames. Player/NPC illumination uses private rendered palettes as explained below. They do not overwrite weather alpha.

The checkout already contains Gen 1–7 ball assets. They remain separate from enabling followers; no duplicate ball system is installed.

For lighting your own maps, see `docs/tutorials/dns.md`. Use `OBJ_EVENT_GFX_LIGHT_SPRITE` events and dedicated window palette references with matching `.pla` files. Keep `palette_refs.json` checksums synchronized through your Kyoto map editor; do not replace expanded metatile data with vanilla Merrp binaries.

## Validation and limits

Built with ARM GCC 12.2.1 and tested in the real mGBA 0.10.5 core. The MP4 uses captured native 240×160 emulator frames, enlarged with nearest-neighbor scaling, without audio. A temporary test fixture supplies normal/shiny Pokémon and demo warps. It is removed from the production source and installer.

Retained validation from the preceding native-icon release: all 2,126 PNG masks/dimensions match their source; 30 ordinary and 30 wide PC icon tile blocks match the imported binaries; eight repeated box-scroll round trips; box chooser, deposit, withdraw, item mode and release animation; four visible normal/shiny contest-result icons; stable night-lamp palette; 15 normal/shiny pairs; five Summary round trips; individual pick-up/swap/placement; box scrolling and eggs; Party allocation cleanup; five Bag round trips with native palettes and restored cloud alpha; transparent ground-shadow pixels; cloud-map exit; and retained day/night window colors. Additional captures cover PC party, held-icon marking and bulk selection.

The current busy Littleroot fixture measures 549 updates per 600 video frames (about 55 field updates per second) while the emulator video runs at 60 frames per second. This is not a guaranteed 60-FPS performance release. Trade and level-up code compile but have not been exercised through complete gameplay sessions. Contest-result rendering is tested, but a complete contest playthrough is not part of this check. Original baseline map-streaming warnings remain for Petalburg–Route104 and Route103–Route110.

Current revision tested in mGBA: all five menu return paths above on daytime, nighttime and foreground-cloud maps (15 combinations), including frame-by-frame checks for a bright incomplete map between black frames; an additional Bag return with the player actively illuminated; private palette release before Bag and PC; independently lit NPCs sharing a native palette; bounded illumination changes while walking into/out of a real lamp; brighter actor pixels in the rendered image; Save cancellation on all three maps; and unchanged approved cloud art, palette and compositor. The video labels the controlled NPC placement and clock fixture. Production files contain no test party, forced clock, boot skip or demo warps.

## Credits

PokéSprite images are © Nintendo / Creatures Inc. / GAME FREAK Inc.; community shiny and variant artwork is credited in the included upstream `CONTRIBUTORS.md`. The upstream code license is included as `LICENSE.txt`. Source: msikma/pokesprite. The PC palette-streaming approach and selected UI resources derive from PokemonSanFran/merrp's icons work. Kyoto's map/Opal/cloud/paraglider systems and existing assets remain in place.
