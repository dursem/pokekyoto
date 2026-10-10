#!/usr/bin/env python3
"""
Tileset capacity tooling.

  resolve   Dump a digest of every layout cell and metatile label resolved to
            (tileset, local metatile index, collision, elevation). Two digests
            taken with different grid formats must be identical for a
            migration to be a no-op in game.
  migrate   Rewrite map.bin/border.bin, metatile_labels.h and raw metatile
            constants from one grid format to another.
  gen       Validate the capacity limits and write src/data/tilesets/tile_cache_info.h (run by make).
  check     Report capacity problems; --verbose also prints each streamed layout's tile cache use.

The eight-word to twelve-subtile metatile conversion lives in metatile_format.py.
"""

import argparse
import hashlib
import json
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))

# Grid formats: (metatile id bits, collision shift, primary metatile count, FRLG primary metatile count)
FORMATS = {
    'legacy': {'id_bits': 10, 'collision_shift': 10, 'collision_bits': 2, 'primary': 512, 'primary_frlg': 640},
    'wide':   {'id_bits': 11, 'collision_shift': 11, 'collision_bits': 1, 'primary': 672, 'primary_frlg': 640},
}

ELEVATION_SHIFT = 12


def path(*parts):
    return os.path.join(ROOT, *parts)


def read_u16(file):
    data = open(file, 'rb').read()
    return [data[i] | (data[i + 1] << 8) for i in range(0, len(data) - 1, 2)]


def write_u16(file, values):
    out = bytearray()
    for v in values:
        out += bytes((v & 0xFF, (v >> 8) & 0xFF))
    open(file, 'wb').write(out)


def unpack_block(block, fmt):
    id_mask = (1 << fmt['id_bits']) - 1
    col_mask = (1 << fmt['collision_bits']) - 1
    return (block & id_mask,
            (block >> fmt['collision_shift']) & col_mask,
            block >> ELEVATION_SHIFT)


def pack_block(metatile, collision, elevation, fmt):
    id_mask = (1 << fmt['id_bits']) - 1
    col_mask = (1 << fmt['collision_bits']) - 1
    if metatile > id_mask or collision > col_mask or elevation > 0xF:
        raise ValueError('block (%d,%d,%d) does not fit format' % (metatile, collision, elevation))
    return metatile | (collision << fmt['collision_shift']) | (elevation << ELEVATION_SHIFT)


def load_layouts():
    return [l for l in json.load(open(path('data/layouts/layouts.json')))['layouts'] if 'blockdata_filepath' in l]


def is_frlg_layout(layout):
    return layout.get('layout_version') == 'frlg'


def tileset_regions():
    """Map tileset symbol -> 'emerald' | 'frlg' | 'common' from the IS_FRLG blocks in headers.h."""
    regions = {}
    region = 'common'
    for line in open(path('src/data/tilesets/headers.h')):
        s = line.strip()
        if s.startswith('#if !IS_FRLG'):
            region = 'emerald'
        elif s.startswith('#else') and region == 'emerald':
            region = 'frlg'
        elif s.startswith('#endif') and region == 'frlg':
            region = 'emerald'
        m = re.match(r'const struct Tileset (\w+)\s*=', s)
        if m:
            regions[m.group(1)] = region
    return regions


def tileset_table():
    """Tileset symbol -> dict(metatiles path, tiles path, isSecondary, isCompressed)."""
    hdr = open(path('src/data/tilesets/headers.h')).read()
    mt = open(path('src/data/tilesets/metatiles.h')).read()
    gfx = open(path('src/data/tilesets/graphics.h')).read()
    incbins = dict(re.findall(r'const u16 (\w+)\[\] = INCBIN_U16\("([^"]+)"\)', mt))
    gfxpaths = dict(re.findall(r'const u32 (\w+)\[\] = INCGFX_U32\("([^"]+)"', gfx))
    rawgfx = {symbol for symbol, _ in re.findall(r'const u32 (\w+)\[\] = INCGFX_U32\("([^"]+)", "\.4bpp"\);', gfx)}
    table = {}
    for m in re.finditer(r'const struct Tileset (\w+)\s*=\s*\{(.*?)\};', hdr, re.S):
        body = m.group(2)
        field = lambda name: (re.search(r'\.%s\s*=\s*(\w+)' % name, body) or [None, None])[1]
        table[m.group(1)] = {
            'metatiles': incbins.get(field('metatiles')),
            'attributes': incbins.get(field('metatileAttributes')),
            'tiles': gfxpaths.get(field('tiles')),
            'rawTilesSymbol': field('tiles') if field('tiles') in rawgfx else None,
            'isSecondary': field('isSecondary') == 'TRUE',
            'isCompressed': field('isCompressed') == 'TRUE',
        }
    return table


def primary_count(fmt, frlg):
    return fmt['primary_frlg'] if frlg else fmt['primary']


def resolve_metatile(metatile, primary, secondary, split):
    if metatile < split:
        return (primary, metatile)
    return (secondary, metatile - split)


def label_groups():
    """Yield (group tileset, label, value, line index) from metatile_labels.h."""
    group = None
    for i, line in enumerate(open(path('include/constants/metatile_labels.h')).read().split('\n')):
        m = re.match(r'//\s*(gTileset_\w+)', line.strip())
        if m:
            group = m.group(1)
            continue
        m = re.match(r'#define\s+(METATILE_\w+)\s+(0x[0-9A-Fa-f]+|\d+)', line.strip())
        if m:
            yield group, m.group(1), int(m.group(2), 0), i


def cmd_resolve(args):
    fmt = FORMATS[args.format]
    regions = tileset_regions()
    digest = {'layouts': {}, 'labels': {}}
    for layout in load_layouts():
        frlg = is_frlg_layout(layout)
        split = primary_count(fmt, frlg)
        cells = []
        for key in ('blockdata_filepath', 'border_filepath'):
            for block in read_u16(path(layout[key])):
                metatile, collision, elevation = unpack_block(block, fmt)
                tileset, local = resolve_metatile(metatile, layout['primary_tileset'], layout['secondary_tileset'], split)
                cells.append('%s:%d:%d:%d' % (tileset, local, collision, elevation))
        digest['layouts'][layout['id']] = hashlib.sha256('\n'.join(cells).encode()).hexdigest()
    for group, label, value, _ in label_groups():
        split = primary_count(fmt, regions.get(group) == 'frlg')
        side = 'P' if value < split else 'S'
        digest['labels'][label] = '%s:%s:%d' % (group, side, value if side == 'P' else value - split)
    json.dump(digest, open(args.out, 'w'), indent=1, sort_keys=True)
    print('resolved %d layouts, %d labels -> %s' % (len(digest['layouts']), len(digest['labels']), args.out))


def cmd_compare(args):
    a, b = json.load(open(args.a)), json.load(open(args.b))
    bad = 0
    for section in ('layouts', 'labels'):
        for key in sorted(set(a[section]) | set(b[section])):
            if a[section].get(key) != b[section].get(key):
                print('MISMATCH %s %s: %s != %s' % (section, key, a[section].get(key), b[section].get(key)))
                bad += 1
    print('%d mismatches' % bad)
    sys.exit(1 if bad else 0)


def migrate_value(value, src, dst, frlg):
    split_src, split_dst = primary_count(src, frlg), primary_count(dst, frlg)
    return value if value < split_src else value - split_src + split_dst


def migrate_prefabs(file, src, dst, regions):
    prefabs = json.load(open(file))
    shifted = 0
    for prefab in prefabs:
        frlg = regions.get(prefab.get('primary_tileset')) == 'frlg'
        for item in prefab.get('metatiles', []):
            if 'metatile_id' in item:
                new = migrate_value(item['metatile_id'], src, dst, frlg)
                shifted += new != item['metatile_id']
                item['metatile_id'] = new
    json.dump(prefabs, open(file, 'w'), indent=4)
    print('%s: %d prefab cells shifted' % (file, shifted))


def grid_inconsistency(fmt):
    """How many map/border cells reference a metatile their tilesets don't have, read in this format."""
    counts = {}
    for symbol, info in tileset_table().items():
        if info['metatiles'] and info['attributes'] and os.path.exists(path(info['attributes'])):
            counts[symbol] = os.path.getsize(path(info['metatiles'])) // (2 * TILES_PER_METATILE)
    bad = 0
    for layout in load_layouts():
        split = primary_count(fmt, is_frlg_layout(layout))
        for key in ('blockdata_filepath', 'border_filepath'):
            for block in read_u16(path(layout[key])):
                metatile = unpack_block(block, fmt)[0]
                if metatile == (1 << fmt['id_bits']) - 1:
                    continue
                if metatile < split:
                    bad += metatile >= counts.get(layout['primary_tileset'], 0)
                else:
                    bad += metatile - split >= counts.get(layout['secondary_tileset'], 0)
    return bad


def cmd_migrate(args):
    src, dst = FORMATS[args.src], FORMATS[args.dst]
    regions = tileset_regions()
    if src is not dst and not args.prefabs_only:
        # A grid already in the destination format reads consistently in it and not in the source one.
        as_src, as_dst = grid_inconsistency(src), grid_inconsistency(dst)
        if as_dst < as_src:
            sys.exit('the map data reads as the %s format (%d out-of-range cells, %d as %s); it looks converted already, '
                     'nothing was changed' % (args.dst, as_dst, as_src, args.src))
    if args.prefabs_only:
        migrate_prefabs(args.prefabs_only, src, dst, regions)
        return

    # Convert everything before writing anything, so data already in the target format is left untouched.
    rewrites = []
    for layout in load_layouts():
        frlg = is_frlg_layout(layout)
        for key in ('blockdata_filepath', 'border_filepath'):
            file = path(layout[key])
            blocks = read_u16(file)
            out = []
            for block in blocks:
                metatile, collision, elevation = unpack_block(block, src)
                try:
                    out.append(pack_block(migrate_value(metatile, src, dst, frlg), collision, elevation, dst))
                except ValueError:
                    sys.exit('%s is not in the %s format (block 0x%04X); nothing was changed' % (layout[key], args.src, block))
            if out != blocks:
                rewrites.append((file, out))
    for file, out in rewrites:
        write_u16(file, out)
    print('rewrote %d map/border files' % len(rewrites))

    labels_path = path('include/constants/metatile_labels.h')
    lines = open(labels_path).read().split('\n')
    relabelled = 0
    for group, label, value, index in label_groups():
        new = migrate_value(value, src, dst, regions.get(group) == 'frlg')
        if new != value:
            lines[index] = re.sub(r'(0x[0-9A-Fa-f]+|\d+)\s*$', '0x%03X' % new, lines[index])
            relabelled += 1
    open(labels_path, 'w').write('\n'.join(lines))
    print('relabelled %d metatile labels' % relabelled)

    for file in args.raw or []:
        lines = open(file).read().split('\n')
        count = 0

        def repl(m):
            nonlocal count
            value = int(m.group(0), 16)
            new = migrate_value(value, src, dst, False)
            if new == value:
                return m.group(0)
            count += 1
            return '0x%03X' % new
        for i, line in enumerate(lines):
            if re.search(r'setmetatile\s*\(|const\s+METATILE_\w+\s*=|#define\s+METATILE_\w+', line):
                lines[i] = re.sub(r'0x[0-9A-Fa-f]{3}\b', repl, line)
        open(file, 'w').write('\n'.join(lines))
        print('%s: %d raw metatile ids shifted' % (os.path.relpath(file, ROOT), count))

    if args.prefabs:
        migrate_prefabs(args.prefabs, src, dst, regions)


# Capacity limits. These mirror include/fieldmap.h and include/tile_cache.h.
LEGACY_TILES_IN_PRIMARY = 512
LEGACY_TILES_TOTAL = 1024
MAX_TILES_IN_PRIMARY = 1024
MAX_TILES_IN_SECONDARY = 2048
MAX_METATILES_IN_PRIMARY = FORMATS['wide']['primary']
MAX_METATILES_IN_SECONDARY = (1 << FORMATS['wide']['id_bits']) - MAX_METATILES_IN_PRIMARY - 1
TILE_CACHE_POOL_SLOTS = 995 - LEGACY_TILES_IN_PRIMARY
TILE_CACHE_WARN_SLOTS = TILE_CACHE_POOL_SLOTS - 48
DOOR_SLOTS = range(LEGACY_TILES_TOTAL - 16, LEGACY_TILES_TOTAL)
TILES_PER_METATILE = 12
# The smol header stores the image size in 14 bits of 4-byte units (include/decompress.h).
MAX_SMOL_IMAGE_BYTES = ((1 << 14) - 1) * 4
TILE_SIZE_4BPP = 32
EXT_TILE_HIGH_SHIFT = 5
EXT_TILE_HIGH_MASK = 0x3
WINDOW = 18
MAP_OFFSET = 7


def png_size(file):
    data = open(file, 'rb').read(24)
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('%s is not a PNG' % file)
    return int.from_bytes(data[16:20], 'big'), int.from_bytes(data[20:24], 'big')


def c_identifier(symbol):
    return symbol[len('gTileset_'):] if symbol.startswith('gTileset_') else symbol


class TilesetData:
    def __init__(self, symbol, info):
        self.symbol = symbol
        self.info = info
        self.dir = os.path.dirname(info['metatiles'])
        self.isSecondary = info['isSecondary']
        self.metatiles = read_u16(path(info['metatiles'])) if info['metatiles'] else []
        self.numMetatiles = len(self.metatiles) // TILES_PER_METATILE
        self.metatileSizeValid = len(self.metatiles) % TILES_PER_METATILE == 0
        tiles_png = path(self.dir, 'tiles.png')
        width, height = png_size(tiles_png) if os.path.exists(tiles_png) else (0, 0)
        self.numTiles = (width // 8) * (height // 8)
        ext_file = path(self.dir, 'metatile_tiles_ext.bin')
        self.extPath = os.path.join(self.dir, 'metatile_tiles_ext.bin') if os.path.exists(ext_file) else None
        self.ext = open(ext_file, 'rb').read() if self.extPath else None
        self.numAttributes = 0
        if info['attributes'] and os.path.exists(path(info['attributes'])):
            self.numAttributes = os.path.getsize(path(info['attributes'])) // 2
        self.obsoleteFiles = [f for f in ('metatile_third_layer.bin', 'metatile_third_layer_ext.bin')
                              if os.path.exists(path(self.dir, f))]

    @property
    def extended(self):
        # Only the extension sidecar can hold tile id bits 10-11, so without it every reference is a legacy one.
        return self.ext is not None

    def virtual_tiles(self, local):
        """The virtual tile ids referenced by one metatile."""
        words = self.metatiles[local * TILES_PER_METATILE:(local + 1) * TILES_PER_METATILE]
        high = self.ext[local * TILES_PER_METATILE:(local + 1) * TILES_PER_METATILE] if self.ext else bytes(TILES_PER_METATILE)
        return [(w & 0x3FF) | (((h >> EXT_TILE_HIGH_SHIFT) & EXT_TILE_HIGH_MASK) << 10) for w, h in zip(words, high)]


def virtual_tile_to_local(tile):
    """(is secondary, local tile index) for a virtual tile id. See include/tile_cache.h."""
    if tile < LEGACY_TILES_IN_PRIMARY:
        return False, tile
    if tile < LEGACY_TILES_TOTAL:
        return True, tile - LEGACY_TILES_IN_PRIMARY
    if tile < LEGACY_TILES_TOTAL + LEGACY_TILES_IN_PRIMARY:
        return False, tile - LEGACY_TILES_IN_PRIMARY
    return True, tile - LEGACY_TILES_TOTAL


def load_maps():
    maps = {}
    for file in sorted(os.listdir(path('data/maps'))):
        map_json = path('data/maps', file, 'map.json')
        if os.path.exists(map_json):
            data = json.load(open(map_json))
            if 'id' in data:
                maps[data['id']] = data
    return maps


def stitched_grid(layout, layouts_by_id, maps_by_layout, maps):
    """The metatile ids the engine's backup map can hold for a layout: map, border and connection strips."""
    width, height = layout['width'], layout['height']
    grid_w, grid_h = width + 2 * MAP_OFFSET, height + 2 * MAP_OFFSET
    border = [b & 0x7FF for b in read_u16(path(layout['border_filepath']))]
    grid = [[border[((x + 1) & 1) + ((y + 1) & 1) * 2] if len(border) == 4 else border[0] for x in range(grid_w)] for y in range(grid_h)]
    blocks = read_u16(path(layout['blockdata_filepath']))
    for y in range(height):
        for x in range(width):
            grid[y + MAP_OFFSET][x + MAP_OFFSET] = blocks[x + y * width] & 0x7FF
    for map_data in maps_by_layout.get(layout['id'], []):
        for connection in map_data.get('connections') or []:
            neighbor_map = maps.get(connection['map'])
            if not neighbor_map:
                continue
            neighbor = layouts_by_id.get(neighbor_map['layout'])
            if not neighbor:
                continue
            nw, nh = neighbor['width'], neighbor['height']
            nblocks = read_u16(path(neighbor['blockdata_filepath']))
            offset = connection['offset']
            for k in range(MAP_OFFSET):
                if connection['direction'] in ('down', 'up'):
                    ny = k if connection['direction'] == 'down' else nh - MAP_OFFSET + k
                    gy = height + MAP_OFFSET + k if connection['direction'] == 'down' else k
                    if not 0 <= ny < nh:
                        continue
                    for nx in range(nw):
                        gx = nx + offset + MAP_OFFSET
                        if 0 <= gx < grid_w:
                            grid[gy][gx] = nblocks[nx + ny * nw] & 0x7FF
                elif connection['direction'] in ('right', 'left'):
                    nx = k if connection['direction'] == 'right' else nw - MAP_OFFSET + k
                    gx = width + MAP_OFFSET + k if connection['direction'] == 'right' else k
                    if not 0 <= nx < nw:
                        continue
                    for ny in range(nh):
                        gy = ny + offset + MAP_OFFSET
                        if 0 <= gy < grid_h:
                            grid[gy][gx] = nblocks[nx + ny * nw] & 0x7FF
    return grid


def pool_tiles_by_metatile(primary, secondary):
    """For each metatile id of a pair, the set of virtual tiles that live in the tile cache pool."""
    cache = {}

    def lookup(metatile):
        if metatile not in cache:
            # field_camera.c draws a metatile its tilesets lack (a connected map's strip) as metatile 0.
            if metatile < MAX_METATILES_IN_PRIMARY:
                tiles = primary.virtual_tiles(metatile if metatile < primary.numMetatiles else 0)
            else:
                local = metatile - MAX_METATILES_IN_PRIMARY
                tiles = secondary.virtual_tiles(local) if local < secondary.numMetatiles else primary.virtual_tiles(0)
            cache[metatile] = frozenset(t for t in tiles if t >= LEGACY_TILES_IN_PRIMARY)
        return cache[metatile]
    return lookup


def worst_window(grid, lookup, size=WINDOW):
    """Most unique pool tiles in any size x size window, and where it is."""
    grid_h, grid_w = len(grid), len(grid[0])
    size_w, size_h = min(size, grid_w), min(size, grid_h)
    best = (0, 0, 0)
    for top in range(grid_h - size_h + 1):
        counts = {}

        def add_column(x, delta):
            for y in range(top, top + size_h):
                for tile in lookup(grid[y][x]):
                    counts[tile] = counts.get(tile, 0) + delta
                    if not counts[tile]:
                        del counts[tile]
        for x in range(size_w):
            add_column(x, 1)
        for left in range(grid_w - size_w + 1):
            if left:
                add_column(left - 1, -1)
                add_column(left + size_w - 1, 1)
            if len(counts) > best[0]:
                best = (len(counts), left - MAP_OFFSET, top - MAP_OFFSET)
    return best


class Report:
    def __init__(self):
        self.errors = []
        self.warnings = []

    def error(self, message):
        self.errors.append(message)

    def warn(self, message):
        self.warnings.append(message)


def analyse(report, verbose=False):
    regions = tileset_regions()
    table = tileset_table()
    tilesets = {}
    for symbol, info in table.items():
        if regions.get(symbol) == 'frlg' or not info['metatiles']:
            continue
        tilesets[symbol] = TilesetData(symbol, info)

    for ts in tilesets.values():
        kind = 'secondary' if ts.isSecondary else 'primary'
        max_tiles = MAX_TILES_IN_SECONDARY if ts.isSecondary else MAX_TILES_IN_PRIMARY
        max_metatiles = MAX_METATILES_IN_SECONDARY if ts.isSecondary else MAX_METATILES_IN_PRIMARY
        if ts.extended and ts.numTiles > max_tiles:
            report.error('%s: %d tiles exceeds the %s limit of %d' % (ts.symbol, ts.numTiles, kind, max_tiles))
        if not ts.metatileSizeValid:
            report.error('%s: metatiles.bin is not a whole number of %d-tile metatiles' % (ts.symbol, TILES_PER_METATILE))
        elif ts.numAttributes and ts.numMetatiles != ts.numAttributes:
            report.error('%s: metatiles.bin holds %d twelve-subtile metatiles but metatile_attributes.bin has %d entries; '
                         'eight-word data must be converted with tools/tilesetcap/metatile_format.py migrate'
                         % (ts.symbol, ts.numMetatiles, ts.numAttributes))
        if ts.obsoleteFiles:
            report.error('%s: %s belong to the retired third-layer format; convert with tools/tilesetcap/metatile_format.py'
                         % (ts.symbol, ', '.join(ts.obsoleteFiles)))
        if ts.numMetatiles > max_metatiles:
            report.error('%s: %d metatiles exceeds the %s limit of %d' % (ts.symbol, ts.numMetatiles, kind, max_metatiles))
        if ts.ext is not None and len(ts.ext) != ts.numMetatiles * TILES_PER_METATILE:
            report.error('%s: metatile_tiles_ext.bin has %d entries but metatiles.bin has %d'
                         % (ts.symbol, len(ts.ext), ts.numMetatiles * TILES_PER_METATILE))
        if ts.extended and not ts.isSecondary and ts.info['isCompressed']:
            report.error('%s: an extended primary tileset must be uncompressed (.isCompressed = FALSE, ".4bpp" tiles)' % ts.symbol)
        if ts.info['isCompressed'] and ts.numTiles * TILE_SIZE_4BPP > MAX_SMOL_IMAGE_BYTES:
            report.error('%s: %d tiles is too large to compress; make it uncompressed (.isCompressed = FALSE, ".4bpp" tiles)'
                         % (ts.symbol, ts.numTiles))

    layouts = [l for l in load_layouts() if not is_frlg_layout(l)]
    layouts_by_id = {l['id']: l for l in layouts}
    maps = load_maps()
    maps_by_layout = {}
    for map_data in maps.values():
        maps_by_layout.setdefault(map_data.get('layout'), []).append(map_data)

    streamed_pairs = set()
    streamed_layouts = []
    for layout in layouts:
        primary, secondary = tilesets.get(layout['primary_tileset']), tilesets.get(layout['secondary_tileset'])
        if not primary or not secondary:
            continue
        if primary.extended or secondary.extended:
            streamed_pairs.add((primary.symbol, secondary.symbol))
            streamed_layouts.append((layout, primary, secondary))

    anims = tileset_animation_slots()
    for layout, primary, secondary in streamed_layouts:
        padding = 0
        for metatile_owner, count in ((primary, primary.numMetatiles), (secondary, secondary.numMetatiles)):
            for local in range(count):
                for tile in metatile_owner.virtual_tiles(local):
                    is_secondary, index = virtual_tile_to_local(tile)
                    owner = secondary if is_secondary else primary
                    if index < owner.numTiles:
                        continue
                    # A legacy (10 bit) reference past the end of a sheet was an empty slot under the stock
                    # loader, and the tile cache draws it empty. An extended reference must have its tile.
                    if tile < LEGACY_TILES_TOTAL:
                        padding += 1
                        continue
                    report.error('%s metatile %d references extended tile %d, but %s has %d tiles'
                                 % (metatile_owner.symbol, local, tile, owner.symbol, owner.numTiles))
                    break
        if padding and verbose:
            print('%s: %d legacy references past the end of a sheet are drawn as empty padding' % (layout['id'], padding))
        door_anims = sorted(set(DOOR_SLOTS) & anims.get(secondary.symbol, set()))
        if door_anims:
            report.warn('%s: %s animates tile slots %d-%d, which door animations on streamed maps also use'
                        % (layout['id'], secondary.symbol, door_anims[0], door_anims[-1]))
        grid = stitched_grid(layout, layouts_by_id, maps_by_layout, maps)
        lookup = pool_tiles_by_metatile(primary, secondary)
        for row in grid:
            for metatile in row:
                if metatile == (1 << FORMATS['wide']['id_bits']) - 1:
                    report.error('%s uses metatile id 0x7FF, which the engine reserves as MAPGRID_UNDEFINED' % layout['id'])
        count, x, y = worst_window(grid, lookup)
        message = '%s: %d cached tiles needed around (%d, %d); the tile cache holds %d' % (layout['id'], count, x, y, TILE_CACHE_POOL_SLOTS)
        if count > TILE_CACHE_POOL_SLOTS:
            report.error(message)
        elif count > TILE_CACHE_WARN_SLOTS:
            report.warn(message + ' (animated tiles also occupy cache slots)')
        elif verbose:
            print(message)

    streamed_ids = {layout['id'] for layout, _, _ in streamed_layouts}
    check_reload_crossings(report, streamed_layouts, layouts_by_id, maps_by_layout, maps, tilesets, verbose)
    for layout, primary, secondary in streamed_layouts:
        for map_data in maps_by_layout.get(layout['id'], []):
            for connection in map_data.get('connections') or []:
                neighbor_map = maps.get(connection['map'])
                neighbor = layouts_by_id.get(neighbor_map['layout']) if neighbor_map else None
                if neighbor and neighbor['id'] not in streamed_ids:
                    report.warn('%s connects to %s, which does not stream its tiles; the view is redrawn when crossing'
                                % (map_data['id'], connection['map']))
                if neighbor and neighbor['primary_tileset'] != layout['primary_tileset']:
                    report.error('%s connects to %s, which uses a different primary tileset' % (map_data['id'], connection['map']))

    for layout in layouts:
        if layout['id'] in streamed_ids:
            continue
        for map_data in maps_by_layout.get(layout['id'], []):
            for connection in map_data.get('connections') or []:
                neighbor_map = maps.get(connection['map'])
                neighbor = layouts_by_id.get(neighbor_map['layout']) if neighbor_map else None
                if neighbor and neighbor['primary_tileset'] != layout['primary_tileset']:
                    report.warn('%s connects to %s, which uses a different primary tileset; the primary is not reloaded when crossing'
                                % (map_data['id'], connection['map']))

    raw_tilesets = sorted({symbol for pair in streamed_pairs for symbol in pair})
    return tilesets, raw_tilesets


ANIM_CONSTANTS = {'NUM_TILES_IN_PRIMARY': str(LEGACY_TILES_IN_PRIMARY), 'TILE_SIZE_4BPP': '32'}


def constant_expression(expr, what):
    """Value of a simple constant expression from src/tileset_anims.c."""
    text = re.sub(r'\b[A-Za-z_]\w*\b', lambda m: ANIM_CONSTANTS.get(m.group(0), m.group(0)), expr)
    if not re.fullmatch(r'[\s\dxXa-fA-F+*()]+', text):
        raise ValueError('src/tileset_anims.c: cannot evaluate %s %r' % (what, expr.strip()))
    return eval(text)


def slot_number(expr):
    return constant_expression(expr, 'animation slot')


def call_arguments(text, start):
    """The comma-separated arguments of the call whose '(' is at text[start]."""
    args, depth, begin = [], 0, start + 1
    for i in range(start, len(text)):
        if text[i] == '(':
            depth += 1
        elif text[i] == ')':
            depth -= 1
            if depth == 0:
                args.append(text[begin:i])
                return args
        elif text[i] == ',' and depth == 1:
            args.append(text[begin:i])
            begin = i + 1
    raise ValueError('src/tileset_anims.c: unterminated call')


def animation_slots_by_init():
    """InitTilesetAnim_* function -> every BG tile slot its animations write, from src/tileset_anims.c."""
    text = open(path('src/tileset_anims.c')).read()
    for name, value in re.findall(r'^#define\s+(\w+)\s+(\w+)\s*$', text, re.M):
        ANIM_CONSTANTS.setdefault(name, value)
    tables = {}
    for m in re.finditer(r'u16 \*const (\w+)\[\] =\s*\{(.*?)\};', text, re.S):
        tables[m.group(1)] = [slot_number(e) for e in re.findall(r'TILE_OFFSET_4BPP\(([^()]*)\)', m.group(2))]
    bodies = {}
    for m in re.finditer(r'^(?:static )?void (\w+)\([^)]*\)\s*\{', text, re.M):
        depth, i = 1, m.end()
        while depth:
            depth += {'{': 1, '}': -1}.get(text[i], 0)
            i += 1
        bodies[m.group(1)] = text[m.end():i - 1]
    direct = {}
    for name, body in bodies.items():
        slots = set()
        if name != 'AppendTilesetAnimToBuffer':
            for call in re.finditer(r'\bAppendTilesetAnimToBuffer\s*\(', body):
                args = call_arguments(body, call.end() - 1)
                if len(args) != 3:
                    raise ValueError('src/tileset_anims.c: %s: unexpected animation transfer' % name)
                dest = args[1].strip()
                count = (constant_expression(args[2], 'animation size') + 31) // 32
                fixed = re.search(r'TILE_OFFSET_4BPP\(([^()]*)\)\)\s*$', dest)
                if fixed:
                    firsts = [slot_number(fixed.group(1))]
                else:
                    table = re.search(r'(\w+)\[[^\]]*\]\s*$', dest)
                    if not table or table.group(1) not in tables:
                        raise ValueError('src/tileset_anims.c: %s: cannot resolve animation destination %r' % (name, dest))
                    firsts = tables[table.group(1)]
                for first in firsts:
                    slots.update(range(first, first + count))
        direct[name] = slots
    result = {}

    def closure(name, seen):
        if name in seen or name not in bodies:
            return set()
        seen.add(name)
        slots = set(direct[name])
        for callee in re.findall(r'\b(\w+)\b', bodies[name]):
            if callee in bodies and callee != name:
                slots |= closure(callee, seen)
        return slots
    for name in bodies:
        if name.startswith('InitTilesetAnim_'):
            result[name] = closure(name, set())
    return result


def tileset_callbacks():
    hdr = open(path('src/data/tilesets/headers.h')).read()
    return {m.group(1): (re.search(r'\.callback\s*=\s*(\w+)', m.group(2)) or [None, None])[1]
            for m in re.finditer(r'const struct Tileset (\w+)\s*=\s*\{(.*?)\};', hdr, re.S)}


def tileset_animation_slots():
    """Tileset symbol -> the BG tile slots its animations write."""
    by_init = animation_slots_by_init()
    return {symbol: by_init.get(callback, set()) for symbol, callback in tileset_callbacks().items() if callback}


def pool_slots_near_edge(layout, primary, secondary, direction, layouts_by_id, maps_by_layout, maps):
    """Tile cache slots the field can show from a layout's cells within a camera ring of an edge.

    Physical slots for a layout that doesn't stream, virtual tiles for one that does; either way each
    is one pool slot still referenced on screen when the player crosses that edge."""
    grid = stitched_grid(layout, layouts_by_id, maps_by_layout, maps)
    grid_h, grid_w = len(grid), len(grid[0])
    lookup = pool_tiles_by_metatile(primary, secondary)
    band = WINDOW + MAP_OFFSET
    rows, cols = range(grid_h), range(grid_w)
    if direction == 'up':
        rows = range(0, min(band, grid_h))
    elif direction == 'down':
        rows = range(max(0, grid_h - band), grid_h)
    elif direction == 'left':
        cols = range(0, min(band, grid_w))
    else:
        cols = range(max(0, grid_w - band), grid_w)
    slots = set()
    for y in rows:
        for x in cols:
            for tile in lookup(grid[y][x]):
                if primary.extended or secondary.extended or tile < TILE_CACHE_POOL_SLOTS + LEGACY_TILES_IN_PRIMARY:
                    slots.add(tile)
    return slots


def check_reload_crossings(report, streamed_layouts, layouts_by_id, maps_by_layout, maps, tilesets, verbose):
    """Crossing into a streamed layout from another secondary tileset redraws the whole view, but the
    previous map's cells keep their tile cache slots until that redraw reaches VRAM. Both must fit."""
    for layout, primary, secondary in streamed_layouts:
        grid = stitched_grid(layout, layouts_by_id, maps_by_layout, maps)
        demand = worst_window(grid, pool_tiles_by_metatile(primary, secondary))[0]
        for origin in maps.values():
            source = layouts_by_id.get(origin.get('layout'))
            if source is None or source['secondary_tileset'] == layout['secondary_tileset']:
                continue
            for connection in origin.get('connections') or []:
                target_map = maps.get(connection['map'])
                if connection['direction'] not in ('up', 'down', 'left', 'right') or not target_map \
                        or target_map.get('layout') != layout['id']:
                    continue
                if source['primary_tileset'] != layout['primary_tileset']:
                    continue
                src_primary, src_secondary = tilesets.get(source['primary_tileset']), tilesets.get(source['secondary_tileset'])
                if not src_primary or not src_secondary:
                    continue
                held = len(pool_slots_near_edge(source, src_primary, src_secondary, connection['direction'],
                                                layouts_by_id, maps_by_layout, maps))
                message = ('%s -> %s: crossing needs %d cached tiles while %d slots still show %s; the tile cache holds %d'
                           % (origin['id'], connection['map'], demand, held, origin['id'], TILE_CACHE_POOL_SLOTS))
                if demand + held > TILE_CACHE_POOL_SLOTS:
                    report.error(message)
                elif demand + held > TILE_CACHE_WARN_SLOTS:
                    report.warn(message)
                elif verbose:
                    print(message)


def cmd_gen(args):
    report = Report()
    tilesets, raw_tilesets = analyse(report)
    for message in report.warnings:
        print('tilesetcap: warning: ' + message, file=sys.stderr)
    for message in report.errors:
        print('tilesetcap: error: ' + message, file=sys.stderr)
    if report.errors:
        sys.exit(1)

    out = ['// Generated by tools/tilesetcap/tilesetcap.py from the tileset and layout data. Do not edit.', '',
           '#if !IS_FRLG', '']
    for symbol in sorted(tilesets):
        out.append('extern const struct Tileset %s;' % symbol)
    out.append('')
    raw_names = {}
    for symbol in raw_tilesets:
        ts = tilesets[symbol]
        if ts.info['rawTilesSymbol']:
            raw_names[symbol] = ts.info['rawTilesSymbol']
            out.append('extern const u32 %s[];' % raw_names[symbol])
        else:
            raw_names[symbol] = 'sTileCacheRawTiles_' + c_identifier(symbol)
            out.append('static const u32 %s[] = INCGFX_U32("%s/tiles.png", ".4bpp");' % (raw_names[symbol], ts.dir))
    for symbol in sorted(tilesets):
        ts = tilesets[symbol]
        if ts.extPath:
            out.append('static const u8 sMetatileTileExt_%s[] = INCBIN_U8("%s");' % (c_identifier(symbol), ts.extPath))
    out += ['', 'const struct TilesetCapacityInfo gTilesetCapacityInfo[] =', '{']
    for symbol in sorted(tilesets):
        ts = tilesets[symbol]
        name = c_identifier(symbol)
        out += ['    {',
                '        .tileset = &%s,' % symbol,
                '        .rawTiles = %s,' % raw_names.get(symbol, 'NULL'),
                '        .tileExt = %s,' % ('sMetatileTileExt_' + name if ts.extPath else 'NULL'),
                '        .numTiles = %d,' % ts.numTiles,
                '        .numMetatiles = %d,' % ts.numMetatiles,
                '        .streamed = %s,' % ('TRUE' if ts.extended else 'FALSE'),
                '    },']
    out += ['};', '', '#else', '',
            'const struct TilesetCapacityInfo gTilesetCapacityInfo[] = {{0}};', '',
            '#endif // !IS_FRLG', '',
            'const u32 gTilesetCapacityInfoCount = ARRAY_COUNT(gTilesetCapacityInfo);', '']
    text = '\n'.join(out)
    if not os.path.exists(args.out) or open(args.out).read() != text:
        open(args.out, 'w').write(text)
    else:
        os.utime(args.out)


def cmd_check(args):
    report = Report()
    tilesets, raw_tilesets = analyse(report, verbose=args.verbose)
    for message in report.warnings:
        print('warning: ' + message)
    for message in report.errors:
        print('error: ' + message)
    extended = [ts.symbol for ts in tilesets.values() if ts.extended]
    print('%d tilesets checked, %d extended, %d need raw tiles for streaming; %d errors, %d warnings'
          % (len(tilesets), len(extended), len(raw_tilesets), len(report.errors), len(report.warnings)))
    sys.exit(1 if report.errors else 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('resolve')
    p.add_argument('--format', choices=FORMATS, required=True)
    p.add_argument('--out', required=True)
    p = sub.add_parser('compare')
    p.add_argument('a')
    p.add_argument('b')
    p = sub.add_parser('migrate')
    p.add_argument('--src', choices=FORMATS, required=True)
    p.add_argument('--dst', choices=FORMATS, required=True)
    p.add_argument('--raw', nargs='*', help='text files whose 3-digit hex literals are absolute Emerald metatile ids')
    p.add_argument('--prefabs', help='porymap prefabs.json to renumber')
    p.add_argument('--prefabs-only', metavar='PREFABS', help='renumber only this porymap prefabs.json')
    p = sub.add_parser('gen')
    p.add_argument('--out', required=True)
    p = sub.add_parser('check')
    p.add_argument('--verbose', action='store_true')
    args = parser.parse_args()
    {'resolve': cmd_resolve, 'compare': cmd_compare, 'migrate': cmd_migrate, 'gen': cmd_gen, 'check': cmd_check}[args.cmd](args)


if __name__ == '__main__':
    main()
