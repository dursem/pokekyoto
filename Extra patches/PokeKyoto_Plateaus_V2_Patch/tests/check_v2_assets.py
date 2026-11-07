#!/usr/bin/env python3
"""Read-only comparison of source payloads with the approved V2 ROM.

Developer test only. Requires Pillow; the installer itself does not.
Usage: python3 tests/check_v2_assets.py /path/to/approved_V2.gba
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
V2_SHA256 = 'a9a17b7691e45063fb867e3eb783d16c0cc00d3e64882a75b500bb22232e12b8'
# Validation-only addresses in the already-approved test ROM; never used to patch it.
POINTERS = {'grass': {'tiles': 4242192, 'map': 4242196},
            'land': {'tiles': 4242768, 'map': 4242772}}

def decode(rom, pos):
    assert rom[pos] == 0x10
    size = int.from_bytes(rom[pos+1:pos+4], 'little'); pos += 4
    out = bytearray(); minimum_distance = 4097
    while len(out) < size:
        flags = rom[pos]; pos += 1
        for bit in range(7, -1, -1):
            if len(out) >= size: break
            if flags & (1 << bit):
                value = (rom[pos] << 8) | rom[pos+1]; pos += 2
                count = (value >> 12) + 3; distance = (value & 4095) + 1
                assert 2 <= distance <= len(out), 'Unsafe/invalid VRAM back-reference'
                minimum_distance = min(minimum_distance, distance)
                for _ in range(count): out.append(out[-distance])
            else:
                out.append(rom[pos]); pos += 1
    assert len(out) == size
    return bytes(out), minimum_distance

def pack_png(path):
    with Image.open(path) as im:
        assert im.mode == 'P'
        width, height = im.size
        assert width % 8 == height % 8 == 0
        pix = im.load(); out = bytearray()
        for y in range(0, height, 8):
            for x in range(0, width, 8):
                for yy in range(8):
                    for xx in range(0, 8, 2):
                        lo, hi = pix[x+xx,y+yy], pix[x+xx+1,y+yy]
                        assert 0 <= lo < 16 and 0 <= hi < 16
                        out.append(lo | (hi << 4))
        return bytes(out)

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('rom',type=Path);args=ap.parse_args()
    rom=args.rom.read_bytes()
    assert hashlib.sha256(rom).hexdigest() == V2_SHA256, 'Not the approved V2 test ROM'
    reports=[]
    for env, pointers in POINTERS.items():
        folder=ROOT/'payload/graphics/battle_environment'/('kyoto_'+env)
        rawtiles,_=decode(rom,struct.unpack_from('<I',rom,pointers['tiles'])[0]-0x08000000)
        rawmap,_=decode(rom,struct.unpack_from('<I',rom,pointers['map'])[0]-0x08000000)
        pngtiles=pack_png(folder/'tiles.png')
        assert pngtiles[:len(rawtiles)] == rawtiles
        assert not any(pngtiles[len(rawtiles):]), 'Nonzero unused tile padding'
        assert (folder/'map.bin').read_bytes() == rawmap
        words=struct.unpack('<2048H',rawmap)
        assert max(x & 1023 for x in words) < len(rawtiles)//32
        reports.append({'environment':env,'referenced_tile_bytes_equal':True,
                        'map_bytes_equal':True,'rom_tile_bytes':len(rawtiles),
                        'source_tile_bytes':len(pngtiles),
                        'unreferenced_zero_padding_bytes':len(pngtiles)-len(rawtiles)})
    print(json.dumps({'approved_v2_sha256':V2_SHA256,'checks':reports},indent=2))
    print('PASS: all referenced pixels and complete tilemaps match the approved V2.')
if __name__=='__main__': main()
