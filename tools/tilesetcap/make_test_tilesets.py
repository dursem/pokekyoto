#!/usr/bin/env python3
"""
Builds the TileCacheTest tilesets and layout used to test streamed tilesets.

Each is a copy of a real tileset (Kyoto's General primary and Fortree secondary, with Route 120 as the
layout) whose tile sheet is doubled: tiles 512-1023 are the horizontally
mirrored tiles 0-511. The secondary sheet is filled up to its 2048-tile limit with vertically (1024-1535)
and doubly (1536-2047) mirrored copies, which only the tile cache tests reference. Every source metatile also gets a mirrored twin that references the mirrored
copies (virtual tile ids 1024+, via metatile_tiles_ext.bin) with the horizontal flip bit toggled, so a
twin looks exactly like its source metatile while every one of its tiles comes from the extended range.

The layout is the source map with its right half drawn using the twins.

The secondary tileset also exercises the extended map palettes (docs/MAP_PALETTES.md): a third set
of metatiles repeats the twins with four of the secondary palettes swapped for the extra logical
palettes 14-17, which are those palettes with red and blue exchanged. The bottom-right corner of the
layout uses them.
"""

import hashlib
import json
import os
import struct
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
import tilesetcap as tc

SOURCE_PRIMARY = 'data/tilesets/primary/general'
SOURCE_SECONDARY = 'data/tilesets/secondary/fortree'
SOURCE_LAYOUT = 'LAYOUT_ROUTE120'
# General's metatiles past the stock 512 are unused by the source layout; leaving them out frees ids for twins.
SOURCE_PRIMARY_METATILES = 512
OUT_PRIMARY = 'data/tilesets/primary/tile_cache_test'
OUT_SECONDARY = 'data/tilesets/secondary/tile_cache_test'
OUT_LAYOUT_DIR = 'data/layouts/TileCacheTest'
LEGACY_TILES = 512
# Route 120's lower right half draws its secondary metatiles with primary palettes 1-4, which secondary
# metatiles may replace with the extra palettes like any other.
PALETTE_SWAPS = {2: 14, 3: 15, 1: 16, 4: 17}
PRIMARY_PALETTES = 6
TINTED_TOP = 44
PALETTE_MANIFEST = 'data/tilesets/map_palettes.json'
SECONDARY_SYMBOL = 'gTileset_TileCacheTest'
HFLIP = 1 << 10
EXT_HIGH_ONE = 1 << tc.EXT_TILE_HIGH_SHIFT


MIRRORS = [lambda box: box, lambda box: box.transpose(Image.FLIP_LEFT_RIGHT),
           lambda box: box.transpose(Image.FLIP_TOP_BOTTOM),
           lambda box: box.transpose(Image.FLIP_LEFT_RIGHT).transpose(Image.FLIP_TOP_BOTTOM)]


def mirrored_sheet(src_png, out_png, copies):
    image = Image.open(tc.path(src_png))
    tiles_per_row = image.width // 8
    out = Image.new('P', (image.width, copies * LEGACY_TILES // tiles_per_row * 8))
    out.putpalette(image.getpalette())
    for tile in range(LEGACY_TILES):
        x, y = (tile % tiles_per_row) * 8, (tile // tiles_per_row) * 8
        if y >= image.height:
            break
        box = image.crop((x, y, x + 8, y + 8))
        for copy in range(copies):
            index = copy * LEGACY_TILES + tile
            out.paste(MIRRORS[copy](box), ((index % tiles_per_row) * 8, (index // tiles_per_row) * 8))
    out.save(tc.path(out_png), optimize=True)


def twin_words(words):
    """A metatile's twin: same words, tile 0 kept, every other tile moved to its mirrored copy and flipped back."""
    out_words, out_ext = [], []
    for word in words:
        if word & 0x3FF == 0:
            out_words.append(word)
            out_ext.append(0)
        else:
            out_words.append(word ^ HFLIP)
            out_ext.append(EXT_HIGH_ONE)
    return out_words, out_ext


def write_tinted_palette(src_pal, out_pal):
    lines = open(tc.path(src_pal)).read().splitlines()
    colours = [line.split() for line in lines[3:19]]
    out = lines[:3] + [' '.join(colours[0])] + ['%s %s %s' % (b, g, r) for r, g, b in colours[1:]]
    open(tc.path(out_pal), 'w').write('\n'.join(out) + '\n')


def add_tinted_twins(out_dir, count, twins):
    """Appends copies of the twins that use the extra palettes, and writes the palette sidecar and manifest entry."""
    words = tc.read_u16(tc.path(out_dir, 'metatiles.bin'))
    attributes = tc.read_u16(tc.path(out_dir, 'metatile_attributes.bin'))
    ext = open(tc.path(out_dir, 'metatile_tiles_ext.bin'), 'rb').read()
    rows = [[word >> 12 for word in words[i:i + tc.TILES_PER_METATILE]] for i in range(0, len(words), tc.TILES_PER_METATILE)]
    first_twin = count * tc.TILES_PER_METATILE
    twin_words = words[first_twin:first_twin + twins * tc.TILES_PER_METATILE]
    twin_ext = ext[first_twin:first_twin + twins * tc.TILES_PER_METATILE]
    tinted_words = []
    for i in range(0, len(twin_words), tc.TILES_PER_METATILE):
        row = []
        for word in twin_words[i:i + tc.TILES_PER_METATILE]:
            logical = PALETTE_SWAPS.get(word >> 12, word >> 12)
            row.append(logical)
            # Porymap leaves palette 0 in the word for extra palettes; the sidecar holds the real one.
            tinted_words.append(word & 0x0FFF if logical >= 14 else word)
        rows.append(row)
    words += tinted_words
    tc.write_u16(tc.path(out_dir, 'metatiles.bin'), words)
    tc.write_u16(tc.path(out_dir, 'metatile_attributes.bin'), attributes + attributes[count:count + twins])
    open(tc.path(out_dir, 'metatile_tiles_ext.bin'), 'wb').write(ext + twin_ext)

    os.makedirs(tc.path(out_dir, 'palettes'), exist_ok=True)
    extras = []
    for source, logical in sorted(PALETTE_SWAPS.items(), key=lambda item: item[1]):
        extra = os.path.join(out_dir, 'palettes', 'extra_%d.pal' % (logical - 14))
        source_dir = SOURCE_PRIMARY if source < PRIMARY_PALETTES else SOURCE_SECONDARY
        write_tinted_palette(os.path.join(source_dir, 'palettes', '%02d.pal' % source), extra)
        extras.append({'day': extra})
    refs = os.path.join(out_dir, 'palette_refs.json')
    checksum = hashlib.sha256(open(tc.path(out_dir, 'metatiles.bin'), 'rb').read()).hexdigest()
    open(tc.path(refs), 'w').write(json.dumps({'version': 1, 'metatiles_sha256': checksum, 'palettes': rows}, indent=2) + '\n')

    manifest = json.load(open(tc.path(PALETTE_MANIFEST)))
    manifest['tilesets'] = [t for t in manifest['tilesets'] if t['tileset'] != SECONDARY_SYMBOL]
    manifest['tilesets'].append({'tileset': SECONDARY_SYMBOL, 'palette_refs': refs, 'palettes': extras,
                                 'effects': [], 'dynamic_metatiles': []})
    open(tc.path(PALETTE_MANIFEST), 'w').write(json.dumps(manifest, indent=2) + '\n')


def build_tileset(src_dir, out_dir, max_metatiles, copies, source_metatiles=None):
    os.makedirs(tc.path(out_dir), exist_ok=True)
    mirrored_sheet(os.path.join(src_dir, 'tiles.png'), os.path.join(out_dir, 'tiles.png'), copies)
    words = tc.read_u16(tc.path(src_dir, 'metatiles.bin'))
    attributes = tc.read_u16(tc.path(src_dir, 'metatile_attributes.bin'))
    if source_metatiles is not None:
        words = words[:source_metatiles * tc.TILES_PER_METATILE]
        attributes = attributes[:source_metatiles]
    count = len(words) // tc.TILES_PER_METATILE
    twins = min(count, max_metatiles - count)
    out_words, out_ext = list(words), [0] * len(words)
    for metatile in range(twins):
        tw, te = twin_words(words[metatile * tc.TILES_PER_METATILE:(metatile + 1) * tc.TILES_PER_METATILE])
        out_words += tw
        out_ext += te
    tc.write_u16(tc.path(out_dir, 'metatiles.bin'), out_words)
    tc.write_u16(tc.path(out_dir, 'metatile_attributes.bin'), attributes[:count] + attributes[:twins])
    open(tc.path(out_dir, 'metatile_tiles_ext.bin'), 'wb').write(bytes(out_ext))
    return count, twins


def main():
    primary_count, primary_twins = build_tileset(SOURCE_PRIMARY, OUT_PRIMARY, tc.MAX_METATILES_IN_PRIMARY,
                                                 tc.MAX_TILES_IN_PRIMARY // LEGACY_TILES, SOURCE_PRIMARY_METATILES)
    secondary_count, secondary_twins = build_tileset(SOURCE_SECONDARY, OUT_SECONDARY, tc.MAX_METATILES_IN_SECONDARY,
                                                     tc.MAX_TILES_IN_SECONDARY // LEGACY_TILES)
    add_tinted_twins(OUT_SECONDARY, secondary_count, secondary_twins)

    layout = next(l for l in tc.load_layouts() if l['id'] == SOURCE_LAYOUT)
    width, height = layout['width'], layout['height']
    blocks = tc.read_u16(tc.path(layout['blockdata_filepath']))
    split = tc.MAX_METATILES_IN_PRIMARY
    used = [b & 0x7FF for b in blocks + tc.read_u16(tc.path(layout['border_filepath']))]
    if any(SOURCE_PRIMARY_METATILES <= m < split for m in used):
        sys.exit('%s uses primary metatiles past %d' % (SOURCE_LAYOUT, SOURCE_PRIMARY_METATILES))
    for y in range(height):
        for x in range(width // 2, width):
            block = blocks[x + y * width]
            metatile = block & 0x7FF
            if metatile < split and metatile < primary_twins:
                metatile += primary_count
            elif metatile >= split and metatile - split < secondary_twins:
                metatile += secondary_count if y < TINTED_TOP else secondary_count + secondary_twins
            blocks[x + y * width] = (block & ~0x7FF) | metatile
    os.makedirs(tc.path(OUT_LAYOUT_DIR), exist_ok=True)
    tc.write_u16(tc.path(OUT_LAYOUT_DIR, 'map.bin'), blocks)
    open(tc.path(OUT_LAYOUT_DIR, 'border.bin'), 'wb').write(open(tc.path(layout['border_filepath']), 'rb').read())
    print('primary: %d metatiles + %d twins; secondary: %d metatiles + %d twins; layout %dx%d'
          % (primary_count, primary_twins, secondary_count, secondary_twins, width, height))


if __name__ == '__main__':
    main()
