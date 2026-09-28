# Pokémon Kyoto Emerald — Opal Porymap v2 (tiles + logical palettes)

Target game baseline: `rh-hideout/pokeemerald-expansion` **Expansion 1.17.0** (`e8bd1cd7b03fc032ea37e3ecd38b379b5d01a1e7`).
Target editor: upstream **Porymap 6.3.1/master** commit `26b919d7ff4b54152de010abd5bf344af2ebe116`.

This package is the Kyoto-specific Porymap/editor half of the Opal tileset-capacity model. It supports Kyoto Emerald's **8-subtile metatiles** and the logical palette sidecar used by M3. It deliberately does **not** fake extra GBA VRAM.

## Intended Kyoto capacities

- physical GBA BG tile window: **1024** tiles, unchanged
- resident physical primary split: **512** tiles, unchanged
- editable/logical primary library: **1024** tiles
- editable/logical secondary library: **2048** tiles
- Porymap logical tile IDs: **0..3071**
- primary metatiles: **672**
- total map-grid IDs: **2048**
- usable secondary metatiles: **1375** (`672..2046`; 2047 is reserved/undefined)
- normal logical palettes: **0..13**
- optional Opal logical palettes: **14..17** through palette reference metadata

Do not change the hardware constant `NUM_TILES_TOTAL` to 3072. The runtime must stream/remap logical source tiles into the normal GBA 1024-tile hardware window.

## Tile numbering

The game-side Opal format preserves the old Emerald numbering, while Porymap shows a more intuitive contiguous editor layout:

- editor `0..1023` = primary `0..1023`
- editor `1024..3071` = secondary `0..2047`

The game-side virtual order remains:

- `0..511`: primary 0..511
- `512..1023`: secondary 0..511
- `1024..1535`: primary 512..1023
- `1536..3071`: secondary 512..2047

The patch converts automatically when loading/saving.

## Extended authoring files

### `metatile_tiles_ext.bin`

The normal `metatiles.bin` keeps the original 10-bit hardware tile field. One byte per sub-tile in `metatile_tiles_ext.bin` stores tile-ID bits 10–11 in bits 5–6. The sidecar is created only when high tile IDs are needed.

### `palette_refs.json`

When the game project enables Opal extra palettes, Porymap reads/writes logical palette IDs 0–17. Logical palettes 14–17 are not new GBA palette banks; the runtime remaps them into available physical BG palette banks. Their metatile hardware palette nibble remains 0 and the logical value lives in `palette_refs.json`.

## Build

### WSL2 (recommended on your current setup)

From this folder:

```bash
chmod +x BUILD_WSL2.sh
./BUILD_WSL2.sh
```

Or patch source without building:

```bash
python3 build_kyoto_porymap.py --patch-only
```

### Windows / Qt

With a Qt development environment on PATH:

```powershell
powershell -ExecutionPolicy Bypass -File .\BUILD_WINDOWS.ps1
```

The source is pinned. The script refuses to patch a different Porymap commit.

## Required game-side Kyoto work

The editor patch must be paired with the game/runtime port. In the Kyoto Expansion 1.17.0 decomp that means:

1. M1 map-grid format: 11-bit metatile ID (`0x07FF`) and one collision bit.
2. `NUM_METATILES_IN_PRIMARY = 672`, total IDs 2048, with 2047 reserved/undefined.
3. M2 Opal tile-cache runtime and capacity metadata.
4. Uncompressed tile data for tilesets that use streaming.
5. `metatile_tiles_ext.bin` compiled into ROM and associated with each extended tileset.
6. Optional M3 palette runtime for logical palettes 14–17.
7. Migration of existing map/border cells before enabling M1, because the collision bit moves.

Do not save an extended project with stock Porymap. High IDs would be truncated/lost.

## Quick self-test

```bash
python3 test_mapping.py
```

Expected:

`PASS: all 3072 logical tile IDs round-trip`

## Qualification after the runtime is installed

Test save -> close Porymap -> reopen -> rebuild ROM. Include primary tile >511, secondary tile >511, IDs near the high end, flips, animated tiles, doors, weather, battle return, connected-map seams, and a cold Continue from a normal in-game save.


## v2 palette fix

The original Opal palette editor patch assumed 12 sub-tiles per metatile. Kyoto Emerald uses 8. v2 removes that hard-coded 12-column assumption: `palette_refs.json` is read and written with exactly the project's metatile column count.

When `data/tilesets/opal_palettes.json` exists, Porymap exposes logical palette IDs **0–17**. For an enabled secondary tileset, IDs **14–17** are stored in `palette_refs.json`, not in the 4-bit hardware palette nibble. The four authored palette files are separate `extra_0.pal` … `extra_3.pal` files.

Do not use logical palettes 14–17 in an in-game map until the matching Kyoto M3 runtime is installed. The editor support and runtime support are separate halves.

## v3 — Kyoto hybrid triple-layer authoring

This package also understands Kyoto's final hybrid triple-layer runtime:

- existing metatiles remain 8 words in `metatiles.bin` and keep layer types 0/1/2 unchanged;
- layer type **3** is exposed as **Triple - Bottom/Middle/Top**;
- the extra four top-layer words are read/written in `metatile_third_layer.bin`;
- extended tile-id bits for that top layer are read/written in `metatile_third_layer_ext.bin`;
- Opal palette rows may be either legacy 8-entry rows or new 12-entry rows; Porymap normalizes them to 12 while editing;
- logical palettes 14–17 and the 1024/2048 logical tile libraries continue to work on the third layer.

The editor deliberately keeps the Layer Type selector visible. Existing Normal/Covered/Split blocks render exactly as before; only blocks set to Triple consume the third layer in-game.
