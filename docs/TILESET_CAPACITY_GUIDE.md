# Tileset Capacity Guide

How many tiles and metatiles a map's tilesets can hold, how tilesets larger than VRAM are drawn, what the
build checks, and how to author and migrate tilesets.

## Limits

| | Primary | Secondary | Where it comes from |
|---|---|---|---|
| Metatiles | 672 | 1375 | 11-bit metatile id in `map.bin` (`MAPGRID_METATILE_ID_MASK`); id 2047 is `MAPGRID_UNDEFINED` |
| Tiles (normal tileset) | 512 | 512 | 10-bit tile id in `metatiles.bin` |
| Tiles (extended tileset) | 1024 | 2048 | tile id bits 10-11 from `metatile_tiles_ext.bin` |
| Tiles in VRAM at once | 512 (always resident) | 483-slot cache, shared with extended primary tiles | one 1024-tile character window for BG1-3 |

A primary/secondary pair can therefore name 3072 source tiles. These are authored tile libraries, not tiles
held in VRAM: `NUM_TILES_TOTAL` stays 1024, and only the first 512 primary tiles are always resident.

## Metatile format

`metatiles.bin` holds twelve little-endian tile words per metatile: four for the bottom layer (BG3), four for
the middle layer (BG2) and four for the top layer (BG1), each in the order top-left, top-right, bottom-left,
bottom-right. The top layer covers object events. Each word is `tile id (bits 0-9) | hflip (10) | vflip (11) |
palette (12-15)`.

The field draws all three layers of every metatile. The layer type in `metatile_attributes.bin` only tells two
independent renderers which two layers a metatile was authored on before metatiles had three: the shop's map
view, where the menu takes BG1, and secret base decoration sprites.

| Layer type | Authored layers |
|---|---|
| `METATILE_LAYER_TYPE_NORMAL` | middle and top |
| `METATILE_LAYER_TYPE_COVERED` | bottom and middle |
| `METATILE_LAYER_TYPE_SPLIT` | bottom and top |

## Map grid format

Each `map.bin` / `border.bin` cell is `metatile id (bits 0-10) | collision (bit 11) | elevation (bits 12-15)`.
Secondary metatile ids start at 672 (`NUM_METATILES_IN_PRIMARY`), so `METATILE_*` labels for secondary
tilesets are 672 + the metatile's index.

A connected map's edge strip is drawn with the current map's tilesets. A strip metatile those tilesets don't
have is drawn as metatile 0, and the validators warn about it.

## Extended tilesets

A tileset becomes extended by having a `metatile_tiles_ext.bin` next to its `metatiles.bin`: one byte per
metatile sub-tile (12 per metatile), in the same order. Bits 5-6 are bits 10-11 of that sub-tile's tile id.
Bits 0-4 and 7 are reserved.

| Tile id | Tile |
|---|---|
| 0-511 | primary tile 0-511 |
| 512-1023 | secondary tile 0-511 |
| 1024-1535 | primary tile 512-1023 |
| 1536-3071 | secondary tile 512-2047 |

Ids below 1024 mean what they always did, so a normal tileset is an extended one whose sidecar is all zero.
Tile N of a sheet is its Nth 8x8 tile in `tiles.png`. The flip and palette bits of a word are never touched by
the extension: the tile cache rewrites only bits 0-9 of a tilemap entry, and the palette allocator only bits
12-15.

Rules the build enforces:

- An extended primary tileset must be uncompressed: `.isCompressed = FALSE` and its tiles included with
  `INCGFX_U32("…/tiles.png", ".4bpp")`.
- A compressed tileset must fit the compressor's 14-bit size field (16383 words of tile data).
- `metatile_tiles_ext.bin` must have exactly 12 bytes per metatile, and `metatiles.bin` exactly one metatile
  per entry of `metatile_attributes.bin`.
- An extended reference (id 1024 or above) must name a tile its sheet has. A legacy reference past the end of
  a sheet draws an empty tile, as the stock loader left those slots empty.

## Editing tilesets

Edit with [opalmap](https://github.com/cynderquil/opalmap), the patched Porymap. It numbers tiles contiguously (primary 0-1023,
secondary 1024-3071), shows full-size sheets, edits all three layers, and reads and writes
`metatile_tiles_ext.bin`, creating it once a tileset first uses a tile past the first 512 of either sheet.
A stock Porymap must not save an extended tileset.

The patched Porytiles (`tools/porytiles`, [PORYTILES_TUTORIAL.md](PORYTILES_TUTORIAL.md)) compiles new
tilesets to the same format.

## How streamed maps are drawn

A layout whose primary or secondary tileset is extended is *streamed* (`TileCache_LayoutIsStreamed`):

- The primary's first 512 tiles are loaded into VRAM slots 0-511 as usual.
- Every other tile is loaded into a slot of the tile cache (slots 512-994) the first time a visible or
  scroll-margin cell on any of the three field tilemaps needs it, and its slot is freed once no tilemap cell
  uses it.
- A freed slot is only reused after the tilemap copy that stopped using it has reached VRAM. The cache records
  the DMA queue's ticket and, on maps with extended palettes, the palette transaction's tilemap ticket, and
  waits for both.
- Slots 995-1023 stay out of the cache: the shop's buy menu draws its frame there and doors use 1008-1023.
  Stock secondary animations that write into this range keep writing it; their tiles are mapped to those
  slots directly.
- Animated tiles keep the VRAM slots `tileset_anims.c` writes them to. When a streamed map loads, its
  animations are run once without writing anything to learn those slots, and each slot is pinned to its
  tile. Animations whose target slots in the pool aren't pinned are skipped rather than overwriting a cached
  tile.

Doors copy their animation frames into tile ids no metatile of the layout references and no animation writes,
falling back to 1008-1023, which streamed maps always use.

## Connections

Walking within one tileset pair, or into a streamed map with the same primary tileset, keeps the cells of the
previous map on screen until they scroll away; the cache keeps their slots. When the palette library changes
at a connection (see [MAP_PALETTES.md](MAP_PALETTES.md)), or the map stops streaming, every map palette bank is
reloaded and the whole view is redrawn in the crossing frame. The primary tileset is never reloaded at a
connection, so connected maps must share it.

## What the build checks

`make` runs `tools/tilesetcap/tilesetcap.py gen`, which writes `src/data/tilesets/tile_cache_info.h` and
fails the build when:

- a tileset has more metatiles or tiles than the limits above, or breaks one of the rules above;
- a tileset still has eight-word metatiles or third-layer files (see Migration);
- a metatile of a streamed layout references an extended tile outside its sheet;
- a streamed layout uses metatile id 2047;
- any 18x18-metatile area of a streamed layout (the camera's 16x16 view plus one row and column being
  scrolled in), including border blocks and connected maps' edges, needs more distinct cached tiles than the
  cache holds; it warns once that is within 48 slots of full, since animation pins also use cache slots;
- crossing into a streamed layout from another secondary tileset needs more cache slots than are free while
  the previous map's cells still hold theirs;
- a streamed layout connects to a map with a different primary tileset.

It warns about any connection between maps with different primary tilesets, and about streamed pairs whose
animations write the door slots.

`make check-map-tilesets` prints the same report and runs the Python tests;
`python3 tools/tilesetcap/tilesetcap.py check --verbose` also prints each streamed layout's worst area and
each crossing's slot budget. Metatiles that scripts place at runtime (`setmetatile`) aren't part of the area
check, so leave headroom on maps that swap in metatiles with different tiles. If the cache ever runs out in
game, the cell shows primary tile 0 and a debug build logs `Tile cache full`.

The generator reads the tileset sources (`*.bin`, `tiles.png`, palettes), layouts, map JSON, map scripts,
`src/data/tilesets/*.h`, `src/graphics.c`, `src/tileset_anims.c` and the grid/tile headers; changing any of
them regenerates it. Generated headers are not committed.

## Migration

Tilesets authored with eight-word metatiles (two layers whose placement came from the layer type), optionally
with `metatile_third_layer.bin` / `metatile_third_layer_ext.bin`, are converted by
`tools/tilesetcap/metatile_format.py`:

```
python3 tools/tilesetcap/metatile_format.py analyze           # what would change; writes nothing
python3 tools/tilesetcap/metatile_format.py migrate           # converts every tileset and its sidecars
python3 tools/tilesetcap/metatile_format.py verify --baseline <checkout of the old tree>
python3 tools/tilesetcap/metatile_format.py render LAYOUT_ROUTE101 --out route101.png
```

`migrate` places each old metatile on the backgrounds the old renderer used (NORMAL: middle and top; COVERED:
bottom and middle; SPLIT: bottom and top; TRIPLE: bottom, middle and the third layer), converts
`metatile_tiles_ext.bin` and `palette_refs.json` (rewriting its checksum) to twelve entries per metatile, and
deletes the third-layer files. The old renderer drew primary tile 20 in palette 3 under every NORMAL cell;
that entry is kept on the bottom layer wherever it could be seen (the middle and top tiles share a transparent
pixel, or one of them is animated) and left empty elsewhere. The tool refuses to run on data that is already
converted or half converted. `verify` renders every metatile of every tileset pairing from both trees and
requires the top and middle layers to match exactly and the bottom layer wherever it can be seen.

Grid data in the old 10-bit format is converted with `tilesetcap.py migrate --src legacy --dst wide`; it
refuses data that already reads as the wide format. `resolve` before and after proves nothing changed:

```
python3 tools/tilesetcap/tilesetcap.py resolve --format legacy --out before.json   # on the old data
python3 tools/tilesetcap/tilesetcap.py resolve --format wide --out after.json      # on the converted data
python3 tools/tilesetcap/tilesetcap.py compare before.json after.json
```

Saves made before the grid change don't restore their saved map view (the metatiles around the player that
scripts had changed); the map loads as authored instead. The metatile format change doesn't touch save data.

## Testing

- `make check TESTS="Tile cache"` runs the tile cache tests: every metatile of the fixture through the cache,
  the boundary tiles of a 1024-tile primary and a 2048-tile secondary, flip/palette preservation, ids past
  3072, delayed DMA and palette commits, scrolling against the legacy renderer, streamed tiles with extra
  palettes, and connection reloads.
- `make check TESTS="Map render"` draws 26 real maps through the field renderer at ten camera positions and
  prints a hash of the screen; the same test builds against an older tree to compare the two.
- `MAP_TILE_CACHE_TEST` (debug warp) is Route 120 with its right half drawn entirely from extended tiles. Both
  halves should look identical. Its bottom-right corner (from row 44) uses the four extra map palettes: greens,
  sands and blues there have red and blue swapped.
- The fixture tilesets are regenerated with `python3 tools/tilesetcap/make_test_tilesets.py`.
