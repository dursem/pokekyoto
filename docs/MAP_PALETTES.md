# Map palettes

Field maps draw from a library of eighteen logical palettes: the fourteen tileset palettes (0–5 primary,
6–13 secondary) and, for secondary tilesets that enable them, four extra palettes 14–17. The hardware has
fourteen map banks, 0–13; banks 14–15 belong to the field UI. Bank 0 always holds palette 0 (the backdrop).
A library can contain more palettes than one screen can show; on maps with an extended library the engine
assigns logical palettes to banks as the camera moves.

Maps whose secondary tileset isn't enabled load palettes 0–13 into banks 0–13 directly. Palette 13 comes from
the secondary tileset's `palettes/13.pal`.

## Enabling extra palettes

In [opalmap](https://github.com/cynderquil/opalmap)'s tileset editor choose **Map palettes → Enable four extra secondary palettes**, use logical
palettes 14–17 on secondary metatiles and edit their colours in the palette editor. Save maps and tilesets,
then run **Validate saved maps and palettes**. The equivalent command line:

```sh
python3 tools/map_palettes/compiler.py --enable gTileset_YourSecondary
make check-map-palettes
make -j
```

Enabling creates `palettes/extra_0.pal`–`extra_3.pal` and `palette_refs.json` next to the secondary's
metatiles and registers them in `data/tilesets/map_palettes.json`. The colours start as copies of `06.pal`.
Map ids and the metatile binary don't change. Porytiles-managed tilesets can set `extra_palettes: true` in
their Porytiles config instead (see `tools/porytiles/README.md`).

```json
{
  "version": 1,
  "tilesets": [{
    "tileset": "gTileset_YourSecondary",
    "palette_refs": "data/tilesets/secondary/your_secondary/palette_refs.json",
    "palettes": [
      {"day": "data/tilesets/secondary/your_secondary/palettes/extra_0.pal"},
      {"day": "data/tilesets/secondary/your_secondary/palettes/extra_1.pal"},
      {"day": "data/tilesets/secondary/your_secondary/palettes/extra_2.pal"},
      {"day": "data/tilesets/secondary/your_secondary/palettes/extra_3.pal"}
    ],
    "effects": [],
    "dynamic_metatiles": []
  }]
}
```

Each extra palette may also give a `night` JASC-PAL file and a `weather` policy: `none`, `dark_contrast`
(default) or `contrast`. A `.pla` file next to a palette flags light colours for time-of-day blending, as for
ordinary tileset palettes. Enabled tilesets today: Petalburg (also streamed, see
[TILESET_CAPACITY_GUIDE.md](TILESET_CAPACITY_GUIDE.md)), Rustboro, and the `TileCacheTest` fixture.

## Data format

`metatiles.bin` keeps twelve tile words per metatile (bottom, middle, top layers). For an enabled secondary,
`palette_refs.json` is authoritative for palette ids: one array of twelve logical ids per metatile, in binary
sub-tile order, and a SHA-256 checksum of the exact metatile binary. The word's palette nibble is 0 for ids
14–17. opalmap writes each file atomically; keep both in the same change. The compiler rejects a row-count or
checksum mismatch.

## Certification

The compiler checks a conservative 18×18-metatile envelope around every camera position, not just the
240×160 screen: the field keeps a 16×16-metatile ring of cells across three tilemaps, and scrolling keeps
trailing cells. Palettes referenced by transparent tiles count too, so set an unused sub-tile's palette to 0.
Every envelope's required palettes, pinned palettes, door palettes and declared mutations must fit beside the
reserved banks. A failure names the map, camera position, logical ids and capacity.

- Primary metatiles use logical 0–13; secondary metatiles may use 0–17.
- **Connections.** A connection between two maps with the same tileset pair keeps the allocation, and the
  previous map's cells stay valid while they scroll away. A connection that changes the library (another
  secondary tileset, or into or out of an enabled one) must keep the primary tileset and grid format; the
  engine then reloads every map bank and redraws the whole view in the crossing frame, so the neighbour's
  strip is certified with this map's tilesets either way. A strip metatile this map's tilesets lack is drawn as
  metatile 0 and reported as a warning.
- `setmetatile` calls with numeric ids or `METATILE_*` constants in the map's own `.pory` file are included
  automatically. Declare any other possible ids (shared scripts, computed values, C) in `dynamic_metatiles`.
- Procedural layouts (Battle Pyramid, Trainer Hill floors) must stay unenabled unless their contents are
  certified.
- Door tables contribute their palettes automatically. Door animations keep their own palette tables; extra
  ids on static door art don't recolour the animation.
- Shops reserve banks 12–13 and keep the whole library resident, because their map view has its own camera.
- Secret base decoration placement is rejected for enabled tilesets.
- An all-bank preview cannot crossfade over an enabled map.

`effects` can list `shop`, `elevator`, `spotlight`, `decoration`, `preview`, `league_lighting` or
`mirage_tower`; direct map scripts and the preview/door tables are detected automatically. `reserved_banks`
lists banks (0–13) an overlay owns, `pinned_palettes` lists logical ids that must keep their own bank number,
and `resident_palettes` lists logical ids needed outside the field tilemaps. Never reserve bank 0.

Current reservations made by field UI on bank 13: the elevator floor window, the decoration menu, partial map
previews and the shop's money window. Sweet Scent's red flash and the weather colour maps treat bank 13 as a
map bank.

## Engine

`tools/map_palettes/compiler.py` writes `src/data/tilesets/map_palettes.generated.h` (included by
`src/tilesets.c`); the build tracks the manifest, palettes, sidecars, metatiles, layouts, maps, scripts and
the door/preview sources.

`src/map_palette.c` owns the allocation on enabled maps. It records the logical palette of every cell of
the three field tilemaps, counts uses per logical id and keeps assignments stable. `DrawMetatile` records a
metatile's twelve references and writes bank numbers without touching tile ids or flips. At the end of the
main callbacks the allocator keeps every still-needed assignment, evicts only unused logical ids, honours
reservations and pins, and loads newly resident palettes with the current time-of-day blend, weather colour
map and the fade coefficient actually applied to the BG palettes. A changed mapping rewrites the bank bits of
all three tilemaps.

The VBlank transaction carries queued door tiles, the three field tilemaps and the palette buffer. While the
main callbacks are editing an enabled map, or once VBlank is past scanline 164, VBlank leaves video alone and
the previous complete frame stays on screen. A tile cache slot released by a cell waits for this transaction
to have been published.

Field effects that read the finished frame — actor lighting and lamp masks (`kyoto_lighting.c`,
`kyoto_lamp_mask.c`), cloud shadows (`kyoto_cloud_layers.c`) and the firefly shade — run after the
callbacks and before the transaction is sealed (`PrepareFieldEffects` in `main.c`). The lamp mask reads map
tiles through `MapPalettesGetTileGraphics`, which returns queued door frames as they will appear.

`LoadMapTilesetPalettes` resets ownership for a full load; heap reset forgets the state without freeing it.
Runtime overflow, an uncertified layout, an undeclared pin or reservation, an invalid reference or a tile
transaction overflow stops in the engine's fatal diagnostic screen; ids are never clamped and required
palettes never evicted.

## Budgets

- Heap: about 3.3 KiB of state on an enabled map; about 1 KiB of stack during reassignment. No extra VRAM.
- ROM: twelve bytes per secondary metatile, 128 bytes of extra day palettes, registries.
- VBlank transaction: 6144 tilemap bytes, up to 512 door-tile bytes and a 1024-byte palette transfer.
- Preparation measured in the test runner: 2.6k cycles with an unchanged mapping, about 114k cycles for a
  full eviction of four palettes (a frame is 280,896 cycles). This is a regression bound, not a guarantee for
  every combination of weather, sprites, animation and UI.

## Verification

```sh
make check-map-palettes
make -j
make check TESTS="Map palettes"
make check TESTS="Tile cache"
```

Emulator and real-hardware QA of enabled maps (walking and biking across seams and reversals, doors, weather,
day/night, fades, shops, previews, battle and menu returns) is still required before treating a new map as
shipping; the automated fixtures don't replace it.
