#!/usr/bin/env python3
"""
Encode, decode and check the data formats of Opal's extended tilesets and map palettes, and
convert map grid data between the 10-bit and 11-bit metatile id formats.

Everything works on raw binary blobs: a metatiles table, a metatile_tiles_ext table, a map's
blockdata, and so on. It never opens or writes a ROM itself. Extract the blobs with HMA or
`rom_blob.py`, convert them, and write them back the same way.

The layout parameters default to Opal's engine (12 tiles per metatile, 512 primary tiles,
672 primary metatiles). Pass the target engine's values explicitly, for example
`--tiles-per-metatile 8 --primary-tiles 640 --primary-metatiles 640` for vanilla FireRed.
Those values describe a port that does not exist yet, so check them against the target's
engine before relying on the output.

Subcommands:
  decode        metatiles (+ ext sidecar, + palette refs) -> JSON listing of every sub-tile
  encode        JSON listing -> metatiles + ext sidecar (+ palette refs)
  refs-check    verify palette_refs.json against its metatiles binary
  refs-build    write palette_refs.json from a metatiles binary (hardware palette nibbles)
  grid-widen    convert blockdata/border cells from 10-bit to 11-bit ids
  grid-narrow   the reverse (refuses cells that do not fit)
  grid-check    report which format a blockdata blob is consistent with
"""

import argparse
import hashlib
import json
import struct
import sys

EXT_HIGH_SHIFT = 5
EXT_HIGH_MASK = 3 << EXT_HIGH_SHIFT
EXT_RESERVED_MASK = 0xFF & ~EXT_HIGH_MASK
TILES_TOTAL = 1024          # hardware: one BG character window holds 1024 tiles
NUM_EXTRA_PALETTES = 4


class FormatError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise FormatError(message)


def read_u16(path):
    data = open(path, 'rb').read()
    require(len(data) % 2 == 0, f'{path}: odd size {len(data)}')
    return list(struct.unpack(f'<{len(data) // 2}H', data))


def write_u16(path, values):
    open(path, 'wb').write(struct.pack(f'<{len(values)}H', *values))


class TileLayout:
    """
    Virtual tile ids: the low 10 bits keep their hardware meaning, bits 10-11 come from the sidecar.

      [0, P)                     primary tiles 0..P-1              (always resident in VRAM)
      [P, 1024)                  secondary tiles 0..1023-P
      [1024, 1024 + (Pe - P))    primary tiles P..Pe-1             (streamed)
      [1024 + (Pe - P), 3072)    secondary tiles 1024-P..Se-1      (streamed)

    P = primary tiles in VRAM, Pe/Se = extended sheet sizes. For Opal P=512, Pe=1024, Se=2048,
    which makes the ranges 0-511, 512-1023, 1024-1535, 1536-3071.

    Opal's src/tile_cache.c GetTileSource() hard-codes the P=512 case through expressions like
    `tile < NUM_TILES_TOTAL + NUM_TILES_IN_PRIMARY`; this class is the general form.
    """

    def __init__(self, primary_tiles=512, primary_extended=1024, secondary_extended=2048):
        self.p = primary_tiles
        self.pe = primary_extended
        self.se = secondary_extended
        self.secondary_base = TILES_TOTAL - self.p
        self.primary_ext_start = TILES_TOTAL
        self.secondary_ext_start = TILES_TOTAL + (self.pe - self.p)
        self.total = self.secondary_ext_start + (self.se - self.secondary_base)
        require(self.total <= 4096, 'layout needs more than 12 bits of tile id')

    def to_sheet(self, virtual):
        """Virtual tile id -> ('primary'|'secondary', local index)."""
        if virtual < self.p:
            return 'primary', virtual
        if virtual < TILES_TOTAL:
            return 'secondary', virtual - self.p
        if virtual < self.secondary_ext_start:
            return 'primary', self.p + (virtual - self.primary_ext_start)
        if virtual < self.total:
            return 'secondary', self.secondary_base + (virtual - self.secondary_ext_start)
        raise FormatError(f'virtual tile {virtual} is past the layout ({self.total} tiles)')

    def to_virtual(self, sheet, local):
        if sheet == 'primary':
            require(0 <= local < self.pe, f'primary tile {local} out of range')
            return local if local < self.p else self.primary_ext_start + (local - self.p)
        require(sheet == 'secondary', f'unknown sheet {sheet}')
        require(0 <= local < self.se, f'secondary tile {local} out of range')
        if local < self.secondary_base:
            return self.p + local
        return self.secondary_ext_start + (local - self.secondary_base)


def decode(metatiles, ext, refs, layout, tiles_per_metatile):
    require(len(metatiles) % tiles_per_metatile == 0,
            f'metatiles: {len(metatiles)} entries is not a multiple of {tiles_per_metatile}')
    if ext is not None:
        require(len(ext) == len(metatiles), f'ext sidecar has {len(ext)} bytes, expected {len(metatiles)}')
    if refs is not None:
        require(len(refs) == len(metatiles) // tiles_per_metatile, 'palette refs row count mismatch')
    out = []
    for m in range(len(metatiles) // tiles_per_metatile):
        subtiles = []
        for s in range(tiles_per_metatile):
            i = m * tiles_per_metatile + s
            word = metatiles[i]
            high = ((ext[i] & EXT_HIGH_MASK) >> EXT_HIGH_SHIFT) if ext is not None else 0
            if ext is not None:
                require(not ext[i] & EXT_RESERVED_MASK, f'metatile {m} sub-tile {s}: reserved ext bits set ({ext[i]:#x})')
            virtual = (word & 0x3FF) | (high << 10)
            sheet, local = layout.to_sheet(virtual)
            entry = {'virtual': virtual, 'sheet': sheet, 'tile': local,
                     'hflip': bool(word & 0x400), 'vflip': bool(word & 0x800),
                     'palette': refs[m][s] if refs is not None else word >> 12}
            if refs is not None:
                entry['hardware_palette_nibble'] = word >> 12
            subtiles.append(entry)
        out.append(subtiles)
    return out


def encode(listing, layout, tiles_per_metatile, extra_palette_first):
    """listing -> (metatile words, ext bytes, refs rows or None)."""
    words, ext, refs = [], [], []
    uses_refs = False
    for m, subtiles in enumerate(listing):
        require(len(subtiles) == tiles_per_metatile, f'metatile {m}: expected {tiles_per_metatile} sub-tiles')
        row = []
        for s, e in enumerate(subtiles):
            virtual = layout.to_virtual(e['sheet'], e['tile'])
            palette = e['palette']
            require(0 <= palette < extra_palette_first + NUM_EXTRA_PALETTES, f'metatile {m} sub-tile {s}: palette {palette}')
            # Extra palettes cannot be expressed in the 4-bit nibble; Opal stores 0 there and keeps the real id in the refs.
            nibble = palette if palette < extra_palette_first else 0
            uses_refs |= palette >= extra_palette_first
            words.append((virtual & 0x3FF) | (0x400 if e.get('hflip') else 0) | (0x800 if e.get('vflip') else 0) | (nibble << 12))
            ext.append((virtual >> 10) << EXT_HIGH_SHIFT)
            row.append(palette)
        refs.append(row)
    return words, ext, (refs if uses_refs else None)


def refs_document(metatile_bytes, rows):
    return {'version': 1, 'metatiles_sha256': hashlib.sha256(metatile_bytes).hexdigest(), 'palettes': rows}


def load_refs(path, metatile_bytes, tiles_per_metatile, palette_count):
    doc = json.load(open(path))
    require(isinstance(doc, dict) and doc.get('version') == 1, f'{path}: expected version 1')
    require(doc.get('metatiles_sha256') == hashlib.sha256(metatile_bytes).hexdigest(),
            f'{path}: metatiles_sha256 does not match the metatiles binary; they were not saved together')
    rows = doc.get('palettes')
    require(isinstance(rows, list) and len(rows) * tiles_per_metatile * 2 == len(metatile_bytes),
            f'{path}: expected one row per metatile')
    for i, row in enumerate(rows):
        require(isinstance(row, list) and len(row) == tiles_per_metatile
                and all(type(v) is int and 0 <= v < palette_count for v in row), f'{path}: bad row {i}')
    return rows


def unpack_cell(value, id_bits):
    id_mask = (1 << id_bits) - 1
    return value & id_mask, (value >> id_bits) & ((1 << (12 - id_bits)) - 1), value >> 12


def convert_grid(values, src_bits, dst_bits, src_split, dst_split, name):
    out = []
    for i, value in enumerate(values):
        metatile, collision, elevation = unpack_cell(value, src_bits)
        # Only the exact all-ones id with nothing else set is MAPGRID_UNDEFINED; id 1023 with elevation is a real metatile.
        if value == (1 << src_bits) - 1:
            out.append((1 << dst_bits) - 1)
            continue
        if metatile >= src_split:
            metatile = metatile - src_split + dst_split
        require(metatile < (1 << dst_bits) - 1, f'{name}[{i}]: metatile {metatile} does not fit {dst_bits} bits')
        collision_bits = 12 - dst_bits
        require(collision < (1 << collision_bits),
                f'{name}[{i}]: collision value {collision} does not fit {collision_bits} bit(s) ({value:#06x})')
        out.append(metatile | (collision << dst_bits) | (elevation << 12))
    return out


def cmd_decode(args):
    layout = TileLayout(args.primary_tiles, args.primary_extended, args.secondary_extended)
    metatiles = read_u16(args.metatiles)
    ext = list(open(args.ext, 'rb').read()) if args.ext else None
    refs = None
    if args.refs:
        refs = load_refs(args.refs, open(args.metatiles, 'rb').read(), args.tiles_per_metatile, args.palette_count)
    listing = decode(metatiles, ext, refs, layout, args.tiles_per_metatile)
    json.dump(listing, open(args.out, 'w') if args.out else sys.stdout, indent=1)
    sheets = {'primary': 0, 'secondary': 0}
    for row in listing:
        for e in row:
            sheets[e['sheet']] = max(sheets[e['sheet']], e['tile'] + 1)
    print(f'{len(listing)} metatiles; highest primary tile {sheets["primary"] - 1}, highest secondary tile {sheets["secondary"] - 1}',
          file=sys.stderr)


def cmd_encode(args):
    layout = TileLayout(args.primary_tiles, args.primary_extended, args.secondary_extended)
    listing = json.load(open(args.listing))
    words, ext, refs = encode(listing, layout, args.tiles_per_metatile, args.extra_palette_first)
    write_u16(args.metatiles, words)
    needs_ext = any(ext)
    if needs_ext or args.always_ext:
        require(args.ext, 'tiles past the first 1024 virtual ids need --ext')
        open(args.ext, 'wb').write(bytes(ext))
    if refs is not None:
        require(args.refs, 'extra palettes are used; pass --refs')
        json.dump(refs_document(open(args.metatiles, 'rb').read(), refs), open(args.refs, 'w'), indent=2)
    print(f'{len(listing)} metatiles; ext sidecar {"written" if needs_ext or args.always_ext else "not needed"}; '
          f'palette refs {"written" if refs is not None else "not needed"}', file=sys.stderr)


def cmd_refs_check(args):
    rows = load_refs(args.refs, open(args.metatiles, 'rb').read(), args.tiles_per_metatile, args.palette_count)
    words = read_u16(args.metatiles)
    bad = [(m, s) for m, row in enumerate(rows) for s, p in enumerate(row)
           if p >= args.extra_palette_first and words[m * args.tiles_per_metatile + s] >> 12 != 0]
    require(not bad, f'extra-palette sub-tiles must keep nibble 0 in the metatiles binary: {bad[:8]}')
    print(f'OK: {len(rows)} rows, checksum matches')


def cmd_refs_build(args):
    data = open(args.metatiles, 'rb').read()
    words = read_u16(args.metatiles)
    require(len(words) % args.tiles_per_metatile == 0, 'metatiles size is not a whole number of metatiles')
    rows = [[w >> 12 for w in words[i:i + args.tiles_per_metatile]] for i in range(0, len(words), args.tiles_per_metatile)]
    json.dump(refs_document(data, rows), open(args.refs, 'w'), indent=2)
    print(f'wrote {len(rows)} rows from the hardware palette nibbles')


def cmd_grid(args, src_bits, dst_bits):
    values = read_u16(args.input)
    out = convert_grid(values, src_bits, dst_bits, args.src_primary_metatiles, args.dst_primary_metatiles, args.input)
    require(args.output or args.in_place, 'pass --output, or --in-place to overwrite the input')
    require(args.in_place or args.output != args.input, 'refusing to overwrite the input without --in-place')
    write_u16(args.output or args.input, out)
    print(f'{len(values)} cells converted')


def cmd_grid_check(args):
    values = read_u16(args.input)
    narrow_hi = sum(1 for v in values if (v >> 10) & 3 in (2, 3))
    wide_ids = sum(1 for v in values if v & 0x400 and (v & 0x7FF) != 0x7FF)
    print(f'{len(values)} cells; cells with bit 11 set: {narrow_hi}; cells with bit 10 set: {wide_ids}')
    print('A 10-bit blob uses bit 10 for collision=1 and bit 11 only for collision 2/3. '
          'Only the two formats together with the known tileset sizes can tell which one a blob is in.')


def add_layout_args(p):
    p.add_argument('--tiles-per-metatile', type=int, default=12, choices=(8, 12))
    p.add_argument('--primary-tiles', type=int, default=512, help='primary tiles resident in VRAM (Opal 512)')
    p.add_argument('--primary-extended', type=int, default=1024)
    p.add_argument('--secondary-extended', type=int, default=2048)
    p.add_argument('--extra-palette-first', type=int, default=14, help='first extra logical palette id (Opal 14)')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)

    p = sub.add_parser('decode')
    add_layout_args(p)
    p.add_argument('metatiles')
    p.add_argument('--ext')
    p.add_argument('--refs')
    p.add_argument('--palette-count', type=int, default=18)
    p.add_argument('--out')
    p.set_defaults(func=cmd_decode)

    p = sub.add_parser('encode')
    add_layout_args(p)
    p.add_argument('listing')
    p.add_argument('--metatiles', required=True)
    p.add_argument('--ext')
    p.add_argument('--refs')
    p.add_argument('--always-ext', action='store_true')
    p.set_defaults(func=cmd_encode)

    p = sub.add_parser('refs-check')
    add_layout_args(p)
    p.add_argument('metatiles')
    p.add_argument('refs')
    p.add_argument('--palette-count', type=int, default=18)
    p.set_defaults(func=cmd_refs_check)

    p = sub.add_parser('refs-build')
    add_layout_args(p)
    p.add_argument('metatiles')
    p.add_argument('refs')
    p.set_defaults(func=cmd_refs_build)

    for name, src, dst in (('grid-widen', 10, 11), ('grid-narrow', 11, 10)):
        p = sub.add_parser(name)
        p.add_argument('input')
        p.add_argument('--output')
        p.add_argument('--in-place', action='store_true')
        p.add_argument('--src-primary-metatiles', type=int, required=True,
                       help='first secondary metatile id before conversion (Opal legacy 512, FireRed 640)')
        p.add_argument('--dst-primary-metatiles', type=int, required=True,
                       help='first secondary metatile id after conversion (Opal wide 672; keep 640 to leave FireRed ids unchanged)')
        p.set_defaults(func=lambda a, s=src, d=dst: cmd_grid(a, s, d))

    p = sub.add_parser('grid-check')
    p.add_argument('input')
    p.set_defaults(func=cmd_grid_check)

    args = parser.parse_args()
    try:
        args.func(args)
    except (FormatError, OSError, json.JSONDecodeError) as error:
        print(f'error: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
