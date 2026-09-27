#!/usr/bin/env python3
"""Opt one Emerald secondary tileset into Kyoto/Opal M2 streaming with an all-zero sidecar.

The zero sidecar preserves every existing 10-bit tile reference. It is a smoke-test step: once
Porymap saves a >1023 game-side virtual tile reference it will update the sidecar's high bits.
"""
from pathlib import Path
import argparse, sys

TILES_PER_METATILE = 8

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('tileset_dir',type=Path,help='e.g. data/tilesets/secondary/petalburg')
    ap.add_argument('--force',action='store_true')
    a=ap.parse_args()
    d=a.tileset_dir.resolve()
    parts=d.as_posix().split('/')
    if 'data' not in parts or 'tilesets' not in parts or 'secondary' not in parts:
        sys.exit('Refusing: this M2 helper only opts in data/tilesets/secondary/... tilesets')
    mt=d/'metatiles.bin'
    if not mt.is_file(): sys.exit(f'Missing {mt}')
    size=mt.stat().st_size
    bytes_per_metatile=TILES_PER_METATILE*2
    if size % bytes_per_metatile:
        sys.exit(f'{mt}: {size} bytes is not a whole number of {TILES_PER_METATILE}-subtile metatiles')
    ext=d/'metatile_tiles_ext.bin'
    expected=size//2
    if ext.exists() and not a.force:
        if ext.stat().st_size==expected:
            print(f'{ext} already exists with the correct {expected} entries; nothing changed.')
            return
        sys.exit(f'{ext} exists with {ext.stat().st_size} bytes, expected {expected}; use --force only if you intend to replace it')
    ext.write_bytes(bytes(expected))
    print(f'Created {ext} ({expected} zero extension entries for {size//bytes_per_metatile} metatiles).')
    print('Rebuild and smoke-test this tileset before placing high tile IDs in Porymap.')

if __name__=='__main__': main()
