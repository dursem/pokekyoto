#!/usr/bin/env python3
"""Reflow indexed GBA 8x8 tiles without changing their numeric tile IDs.

Example:
    python3 tools/tilesetcap/reflow_primary_png.py my_1024x128.png \
      data/tilesets/primary/general/tiles.png --width 128

A source 1024x128 sheet becomes the 128x1024 layout expected by PokéKyoto/Porymap.
It is NOT a rotation: row-major 8x8 tile order is preserved exactly.
"""

import argparse
from pathlib import Path
from PIL import Image


def reflow(src, dst, out_width=128):
    with Image.open(src) as image:
        if image.mode != 'P':
            raise ValueError('tileset must be indexed PNG (mode P), not RGB/RGBA')
        if image.width % 8 or image.height % 8 or out_width % 8:
            raise ValueError('all dimensions must be multiples of 8')
        count = image.width * image.height // 64
        if count != 2048:
            raise ValueError(f'expected exactly 2048 tiles (1024x128 or 128x1024); got {count}')
        if count % (out_width // 8):
            raise ValueError('output width does not divide the tile count')
        if image.getextrema()[1] > 15:
            raise ValueError('GBA tiles must use palette indices 0..15; image uses larger indices')
        out_height = count * 64 // out_width
        result = Image.new('P', (out_width, out_height), 0)
        result.putpalette(image.getpalette())
        original_columns = image.width // 8
        output_columns = out_width // 8
        for tile in range(count):
            sx = (tile % original_columns) * 8
            sy = (tile // original_columns) * 8
            dx = (tile % output_columns) * 8
            dy = (tile // output_columns) * 8
            result.paste(image.crop((sx, sy, sx + 8, sy + 8)), (dx, dy))
        if 'transparency' in image.info:
            result.info['transparency'] = image.info['transparency']
        Path(dst).parent.mkdir(parents=True, exist_ok=True)
        result.save(dst, optimize=True)
    print(f'{count} tiles preserved: {src} -> {dst} ({out_width}x{out_height})')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('input', help='indexed 1024x128 or 128x1024 source PNG')
    ap.add_argument('output', help='converted indexed tiles.png')
    ap.add_argument('--width', type=int, default=128, help='output sheet width in pixels (default: 128)')
    args = ap.parse_args()
    reflow(args.input, args.output, args.width)


if __name__ == '__main__':
    main()
