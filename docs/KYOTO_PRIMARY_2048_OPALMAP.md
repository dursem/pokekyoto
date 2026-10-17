# PokéKyoto primary tileset upgrade: 2,048 tiles (128×1024 pixels)

## What changed

This patch doubles the **editable primary sheet's 8×8 tile capacity** from 1,024 to **2,048** without moving any existing map or metatile IDs. The primary `General` PNG was padded with 1,024 *blank*, index-0 tiles, preserving the first 1,024 tiles pixel-for-pixel. The 672 primary 16×16 metatile slots, and all secondary graphics and maps, are unchanged.

- Source sheet: `data/tilesets/primary/general/tiles.png` is now **128×1024 px**, i.e. 16 columns × 128 rows of 8×8 tiles.
- For artwork stored as **1024×128 px**, run the `reflow_primary_png.py` helper (below). Changing the PNG dimensions alone is insufficient: you must preserve 8×8 tile order.
- Other primary tilesets are still their original physical image sizes. The engine permits expanded primary tilesets up to the new limit, but **does not automatically overwrite** their assets.
- There are now 2,048 primary + 2,048 secondary logical 8×8 tiles, addressed by a 12-bit virtual ID (0..4095).
- Actual GBA background VRAM still holds 1,024 map tiles. The existing Kyoto streaming cache handles the surplus. This does *not* allow 4,096 distinct tiles to be visible simultaneously.
- `NUM_TILES_IN_PRIMARY` stays at 512 for the resident hardware split. `NUM_TILES_IN_PRIMARY_EXTENDED` is now 2048. `NUM_TILES_TOTAL` must remain 1024.
- The source validator allows 2,048 primary tiles and refuses invalid high tile references or overflowing streamed-map cache windows.

## Backwards-compatible game virtual indices

| Virtual ID | Physical source | Local 8×8 index |
| ---: | --- | ---: |
| 0–511 | primary | 0–511 |
| 512–1023 | secondary | 0–511 |
| 1024–1535 | primary | 512–1023 |
| 1536–3071 | secondary | 512–2047 |
| **3072–4095** | **primary** | **1024–2047** |

Existing virtual IDs 0–3071 were **not renumbered**. For a primary source tile at local index 1024, encode virtual tile **3072 (`0xC00`)**. The normal metatile word holds its lower 10 bits; `metatile_tiles_ext.bin` bits 5–6 hold tile ID bits 10–11, so the extension byte uses `0x60`. These are the same existing two extension bits; no new metatile file format is necessary.

## OpalMap / Porymap editor compatibility

**The existing OpalMap/Porymap application will not automatically learn the new capacity by opening the updated project.** The previous customized Opal editor exposed 1,024 primary + 2,048 secondary logical tiles. To browse and edit all 4,096 tiles, its source needs an additional change and a rebuilt editor. No OpalMap executable is included in this patch.

The editor should expose **contiguous editor IDs**: primary 0–2047, secondary 2048–4095. It must translate them to the unchanged virtual IDs above when editing `metatiles.bin` and `metatile_tiles_ext.bin`. Mapping for editor developers:

```python
def editor_to_virtual(tile):
    if not 0 <= tile < 4096:
        raise ValueError(tile)
    if tile < 512:    return tile               # primary 0..511
    if tile < 1024:   return tile + 512         # primary 512..1023
    if tile < 2048:   return tile + 2048        # primary 1024..2047
    if tile < 2560:   return tile - 1536        # secondary 0..511
    return tile - 1024                        # secondary 512..2047


def virtual_to_editor(tile):
    if not 0 <= tile < 4096:
        raise ValueError(tile)
    if tile < 512:    return tile
    if tile < 1024:   return tile + 1536
    if tile < 1536:   return tile - 512
    if tile < 3072:   return tile + 1024
    return tile - 2048
```

Porymap/OpalMap also needs to use the expanded source image height (128×1024), show all 2,048 tiles in its primary 8×8 tile picker, and permit new high virtual IDs in the 12-bit sidecar. Preserve the existing 672 primary metatiles and 11-bit map-grid codec. Do **not** just bump a vanilla emulator VRAM count or change stock Emerald's global tile ID split.

## Importing artwork from a 1024×128 landscape sheet

Artwork must be indexed (P-mode PNG) with indices 0..15. Example from PokéKyoto's repository root:

```bash
python3 tools/tilesetcap/reflow_primary_png.py /path/to/primary_1024x128.png \
  data/tilesets/primary/general/tiles.png --width 128
```

This is a rearrangement of 8×8 tiles, **not a rotation or image resize**. Keep the tile indices of existing mapped artwork intact when replacing the entire sheet. Always make a backup of `tiles.png` first. A completely new sheet with different tile order will change what your current metatiles display.

## Checking the game-source patch

```bash
python3 -m unittest tools/tilesetcap/test_tilesetcap.py
python3 tools/tilesetcap/tilesetcap.py check
make -j2
```

A successful source checker does not prove the whole ROM has run in mGBA. Test old maps, map transitions, doors, tile animations, and a metatile deliberately using **primary tile 1024 (virtual 3072)**. Animated tiles and streamed-map VRAM constraints remain in force. The external editor requires its own implementation and GUI tests before claiming compatibility.
