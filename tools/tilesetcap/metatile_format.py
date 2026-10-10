#!/usr/bin/env python3
"""
Metatile format migration: eight-word metatiles to twelve-subtile metatiles.

The old format stored two 2x2 layers per metatile (8 tile words) and the field renderer chose
which backgrounds they went to from the metatile's layer type. A tileset could also carry a
separate third 2x2 layer (metatile_third_layer.bin, metatile_third_layer_ext.bin) for TRIPLE
metatiles. The destination format stores all three layers in metatiles.bin, in the order
bottom (BG3), middle (BG2), top (BG1), and the renderer ignores the layer type.

  analyze   Report what a migration would do, without writing anything.
  migrate   Convert every tileset (metatiles.bin, metatile_tiles_ext.bin, palette_refs.json,
            third-layer files). Refuses to run on data that is already converted or mixed.
  verify    Render every metatile of every tileset pairing from a baseline copy of the old data
            and from the current data, and require identical output (see VERIFY below).
  render    Render one layout to a PNG (either format), for visual comparison.

How each old layer type is placed (w = old words, t = third layer, F = filler):

  NORMAL  bottom F,     middle w[0..3], top w[4..7]
  COVERED bottom w[0..3], middle w[4..7], top 0
  SPLIT   bottom w[0..3], middle 0,       top w[4..7]
  TRIPLE  bottom w[0..3], middle w[4..7], top t[0..3] (0 without a third layer)

The old renderer wrote the fixed entry 0x3014 (primary tile 20, palette 3) to BG3 under every
NORMAL cell. It is kept (F = 0x3014) in each 8x8 cell where it can be seen: where the middle and
top tiles share a transparent pixel, or where either is a tile animation slot whose pixels change
at run time. Elsewhere F = 0, which leaves the bottom layer empty in the editor.

VERIFY compares, per metatile and cell: the top and middle layers' tile entries and pixels exactly,
and the bottom layer wherever it can be seen. It also checks behaviour attributes are unchanged.
"""

import argparse
import hashlib
import json
import os
import re
import struct
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tilesetcap as tc  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))

OLD_WORDS = 8
NEW_WORDS = 12
THIRD_WORDS = 4
NORMAL, COVERED, SPLIT, TRIPLE = 0, 1, 2, 3
LAYER_NAMES = {NORMAL: 'NORMAL', COVERED: 'COVERED', SPLIT: 'SPLIT', TRIPLE: 'TRIPLE'}
NORMAL_FILLER = 0x3014
TILE_MASK = 0x3FF
HFLIP, VFLIP = 0x400, 0x800
EXT_HIGH_SHIFT = 5
LEGACY_PRIMARY_TILES = 512
LEGACY_TOTAL_TILES = 1024
PRIMARY_PALS = 6
PRIMARY_PALS_FRLG = 7
PRIMARY_METATILES = 672
PRIMARY_METATILES_FRLG = 640


class MigrationError(Exception):
    pass


def fail(message):
    raise MigrationError(message)


def path(*parts):
    return os.path.join(ROOT, *parts)


def read_words(file):
    data = open(file, 'rb').read()
    if len(data) % 2:
        fail('%s: odd size' % file)
    return list(struct.unpack('<%dH' % (len(data) // 2), data))


def pack_words(values):
    return struct.pack('<%dH' % len(values), *values)


def write_atomic(file, data):
    tmp = file + '.migrating'
    with open(tmp, 'wb') as f:
        f.write(data)
    os.replace(tmp, file)


# ---------------------------------------------------------------------------------------------
# Project discovery


def c_sources():
    return [path('src/data/tilesets/headers.h'), path('src/data/tilesets/metatiles.h'),
            path('src/data/tilesets/graphics.h'), path('src/graphics.c')]


def tileset_regions():
    regions, region = {}, 'common'
    for line in open(path('src/data/tilesets/headers.h')):
        s = line.strip()
        if s.startswith('#if !IS_FRLG'):
            region = 'emerald'
        elif s.startswith('#else') and region == 'emerald':
            region = 'frlg'
        elif s.startswith('#endif') and region in ('emerald', 'frlg'):
            region = 'common'
        m = re.match(r'const struct Tileset (\w+)\s*=', s)
        if m:
            regions[m.group(1)] = region
    return regions


def project_tilesets():
    """Tileset symbol -> metatiles/attributes/tiles/palette paths and flags."""
    hdr = open(path('src/data/tilesets/headers.h')).read()
    mt = open(path('src/data/tilesets/metatiles.h')).read()
    gfx = open(path('src/data/tilesets/graphics.h')).read() + open(path('src/graphics.c')).read()
    bins = dict(re.findall(r'const u16 (\w+)\[\] = INCBIN_U16\("([^"]+)"\)', mt))
    tiles = dict(re.findall(r'const u32 (\w+)\[\] = INCGFX_U32\("([^"]+)"', gfx))
    palettes = {}
    for m in re.finditer(r'const u16 (?:ALIGNED\(4\) )?(\w+)\[\]\[16\] =\s*\{(.*?)\};', gfx, re.S):
        palettes[m.group(1)] = re.findall(r'INCGFX_U16\("([^"]+)"', m.group(2))
    regions = tileset_regions()
    result = {}
    for m in re.finditer(r'const struct Tileset (\w+)\s*=\s*\{(.*?)\};', hdr, re.S):
        body = m.group(2)

        def field(name):
            f = re.search(r'\.%s\s*=\s*(\w+)' % name, body)
            return f.group(1) if f else None
        symbol = m.group(1)
        result[symbol] = {
            'symbol': symbol,
            'metatiles': bins.get(field('metatiles')),
            'attributes': bins.get(field('metatileAttributes')),
            'tiles': tiles.get(field('tiles')),
            'palettes': palettes.get(field('palettes'), []),
            'isSecondary': field('isSecondary') == 'TRUE',
            'frlg': regions.get(symbol) == 'frlg',
        }
    return result


def load_layouts():
    return [l for l in json.load(open(path('data/layouts/layouts.json')))['layouts'] if 'blockdata_filepath' in l]


def palette_manifest():
    file = path('data/tilesets/map_palettes.json')
    if not os.path.exists(file):
        return {}
    return {entry['tileset']: entry for entry in json.load(open(file)).get('tilesets', [])}


class MetatileFile:
    """One metatiles.bin with its attributes and sidecars (shared by every tileset that INCBINs it)."""

    def __init__(self, metatiles, attributes):
        self.path = metatiles
        self.dir = os.path.dirname(metatiles)
        self.attributesPath = attributes
        self.words = read_words(path(metatiles))
        self.attributes = open(path(attributes), 'rb').read()
        self.extPath = os.path.join(self.dir, 'metatile_tiles_ext.bin')
        self.ext = open(path(self.extPath), 'rb').read() if os.path.exists(path(self.extPath)) else None
        self.thirdPath = os.path.join(self.dir, 'metatile_third_layer.bin')
        self.third = read_words(path(self.thirdPath)) if os.path.exists(path(self.thirdPath)) else None
        self.thirdExtPath = os.path.join(self.dir, 'metatile_third_layer_ext.bin')
        self.thirdExt = open(path(self.thirdExtPath), 'rb').read() if os.path.exists(path(self.thirdExtPath)) else None
        self.refsPath = os.path.join(self.dir, 'palette_refs.json')
        self.refs = json.load(open(path(self.refsPath))) if os.path.exists(path(self.refsPath)) else None
        self.format, self.count, self.attrWidth = self.detect()

    def detect(self):
        """Which format the words are in, from the attribute count, which the migration never changes."""
        nwords, nbytes = len(self.words), len(self.attributes)
        candidates = []
        for fmt, per in (('old', OLD_WORDS), ('new', NEW_WORDS)):
            if nwords % per:
                continue
            count = nwords // per
            for width in (2, 4):
                if count * width == nbytes:
                    candidates.append((fmt, count, width))
        if len(candidates) != 1:
            fail('%s: cannot tell the metatile format (%d words, %d attribute bytes)' % (self.path, nwords, nbytes))
        return candidates[0]

    def layer_type(self, index):
        w = self.attrWidth
        value = int.from_bytes(self.attributes[index * w:(index + 1) * w], 'little')
        return (value >> 12) & 0xF if w == 2 else (value >> 29) & 3

    def old_metatile(self, index):
        words = self.words[index * OLD_WORDS:(index + 1) * OLD_WORDS]
        ext = list(self.ext[index * OLD_WORDS:(index + 1) * OLD_WORDS]) if self.ext else [0] * OLD_WORDS
        third = self.third[index * THIRD_WORDS:(index + 1) * THIRD_WORDS] if self.third else None
        thirdExt = list(self.thirdExt[index * THIRD_WORDS:(index + 1) * THIRD_WORDS]) if self.thirdExt else [0] * THIRD_WORDS
        return words, ext, third, thirdExt

    def validate_old(self):
        if self.ext is not None and len(self.ext) != self.count * OLD_WORDS:
            fail('%s: has %d entries, expected %d (8 per metatile)' % (self.extPath, len(self.ext), self.count * OLD_WORDS))
        if self.third is not None and len(self.third) != self.count * THIRD_WORDS:
            fail('%s: has %d words, expected %d' % (self.thirdPath, len(self.third), self.count * THIRD_WORDS))
        if self.thirdExt is not None and self.third is None:
            fail('%s exists without %s' % (self.thirdExtPath, self.thirdPath))
        if self.thirdExt is not None and len(self.thirdExt) != self.count * THIRD_WORDS:
            fail('%s: has %d entries, expected %d' % (self.thirdExtPath, len(self.thirdExt), self.count * THIRD_WORDS))
        for i in range(self.count):
            layer = self.layer_type(i)
            if layer not in LAYER_NAMES:
                fail('%s: metatile %d has layer type %d, which the old renderer never drew' % (self.path, i, layer))
            if layer == TRIPLE and self.attrWidth == 4:
                fail('%s: metatile %d is TRIPLE in an FRLG tileset, which has no such layer type' % (self.path, i))
        if self.refs is not None:
            rows = self.refs.get('palettes')
            if not isinstance(rows, list) or len(rows) != self.count:
                fail('%s: expected %d rows' % (self.refsPath, self.count))
            if self.refs.get('metatiles_sha256') != hashlib.sha256(open(path(self.path), 'rb').read()).hexdigest():
                fail('%s: checksum does not match %s; fix the pairing before migrating' % (self.refsPath, self.path))
            for i, row in enumerate(rows):
                if not isinstance(row, list) or len(row) not in (OLD_WORDS, OLD_WORDS + THIRD_WORDS):
                    fail('%s: row %d needs 8 or 12 palette ids' % (self.refsPath, i))
                if any(not isinstance(v, int) or not 0 <= v < 18 for v in row):
                    fail('%s: row %d has a palette id outside 0-17' % (self.refsPath, i))


# ---------------------------------------------------------------------------------------------
# Graphics


def load_sheet(file):
    """Tiles of an indexed tiles.png, shape (count, 8, 8) of colour indices."""
    if not file or not os.path.exists(path(file)):
        return np.zeros((0, 8, 8), np.uint8)
    image = Image.open(path(file))
    if image.mode != 'P':
        fail('%s: expected an indexed PNG' % file)
    pixels = np.array(image, dtype=np.uint8) & 0xF
    h, w = pixels.shape
    return pixels.reshape(h // 8, 8, w // 8, 8).transpose(0, 2, 1, 3).reshape(-1, 8, 8)


def load_jasc(file):
    lines = open(path(file)).read().split()
    if lines[:3] != ['JASC-PAL', '0100', '16']:
        fail('%s: not a 16-colour JASC palette' % file)
    values = [int(v) for v in lines[3:3 + 48]]
    return np.array(values, np.uint8).reshape(16, 3)


slot_number = tc.slot_number
tileset_animation_slots = tc.animation_slots_by_init
tileset_callbacks = tc.tileset_callbacks


class Sheets:
    """Tile pixels for a (primary, secondary) pairing, addressed by game-side virtual tile id."""

    def __init__(self, primary, secondary, anim=frozenset()):
        self.anim = anim
        self.primary = load_sheet(primary['tiles']) if primary else np.zeros((0, 8, 8), np.uint8)
        self.secondary = load_sheet(secondary['tiles']) if secondary else np.zeros((0, 8, 8), np.uint8)
        self.hasPrimary, self.hasSecondary = primary is not None, secondary is not None

    def tile(self, virtual):
        """Pixels of a virtual tile, or None when the pairing doesn't say (no tileset on that side)."""
        if virtual < LEGACY_PRIMARY_TILES:
            sheet, local, known = self.primary, virtual, self.hasPrimary
        elif virtual < LEGACY_TOTAL_TILES:
            sheet, local, known = self.secondary, virtual - LEGACY_PRIMARY_TILES, self.hasSecondary
        elif virtual < LEGACY_TOTAL_TILES + LEGACY_PRIMARY_TILES:
            sheet, local, known = self.primary, virtual - LEGACY_PRIMARY_TILES, self.hasPrimary
        else:
            sheet, local, known = self.secondary, virtual - LEGACY_TOTAL_TILES, self.hasSecondary
        if not known:
            return None
        if local >= len(sheet):
            # Past the end of the sheet: the stock loader left these slots blank.
            return np.zeros((8, 8), np.uint8)
        return sheet[local]


def oriented(pixels, word):
    if word & HFLIP:
        pixels = pixels[:, ::-1]
    if word & VFLIP:
        pixels = pixels[::-1, :]
    return pixels


def virtual(word, ext):
    return (word & TILE_MASK) | (((ext >> EXT_HIGH_SHIFT) & 3) << 10)


# ---------------------------------------------------------------------------------------------
# Conversion


def old_placement(layer, words, ext, third, thirdExt, refs):
    """[(bottom, middle, top)] per cell of (word, ext, logical palette) as the old renderer drew it."""
    def cell(source, sourceExt, sourceRefs, i):
        word = source[i]
        return (word, sourceExt[i], sourceRefs[i] if sourceRefs is not None else word >> 12)
    empty = (0, 0, 0)
    lowRefs = refs[:OLD_WORDS] if refs is not None else None
    highRefs = refs[OLD_WORDS:] if refs is not None and len(refs) > OLD_WORDS else None
    cells = []
    for i in range(4):
        if layer == NORMAL:
            cells.append((None, cell(words, ext, lowRefs, i), cell(words, ext, lowRefs, 4 + i)))
        elif layer == COVERED:
            cells.append((cell(words, ext, lowRefs, i), cell(words, ext, lowRefs, 4 + i), empty))
        elif layer == SPLIT:
            cells.append((cell(words, ext, lowRefs, i), empty, cell(words, ext, lowRefs, 4 + i)))
        else:
            top = empty
            if third is not None:
                top = (third[i], thirdExt[i], highRefs[i] if highRefs is not None else third[i] >> 12)
            cells.append((cell(words, ext, lowRefs, i), cell(words, ext, lowRefs, 4 + i), top))
    return cells


def filler_needed(middle, top, sheets_list):
    """Whether the old NORMAL filler can be seen through a middle/top cell pair in any pairing."""
    for sheets in sheets_list:
        for entry in (middle, top):
            if (entry[0] & TILE_MASK) in sheets.anim and virtual(entry[0], entry[1]) < LEGACY_TOTAL_TILES:
                return True
        mid = sheets.tile(virtual(middle[0], middle[1]))
        upper = sheets.tile(virtual(top[0], top[1]))
        if mid is None or upper is None:
            return True
        if np.any((oriented(mid, middle[0]) == 0) & (oriented(upper, top[0]) == 0)):
            return True
    return False


def convert(mfile, sheets_list, stats):
    words, ext, refs = [], [], []
    useExt = mfile.ext is not None or mfile.thirdExt is not None
    oldRows = mfile.refs['palettes'] if mfile.refs is not None else None
    for index in range(mfile.count):
        layer = mfile.layer_type(index)
        stats[LAYER_NAMES[layer]] = stats.get(LAYER_NAMES[layer], 0) + 1
        oldWords, oldExt, third, thirdExt = mfile.old_metatile(index)
        cells = old_placement(layer, oldWords, oldExt, third, thirdExt, oldRows[index] if oldRows else None)
        layers = [[None] * 4 for _ in range(3)]
        for i, (bottom, middle, top) in enumerate(cells):
            if bottom is None:
                if filler_needed(middle, top, sheets_list):
                    bottom = (NORMAL_FILLER, 0, NORMAL_FILLER >> 12)
                    stats['filler kept'] = stats.get('filler kept', 0) + 1
                else:
                    bottom = (0, 0, 0)
                    stats['filler dropped'] = stats.get('filler dropped', 0) + 1
            layers[0][i], layers[1][i], layers[2][i] = bottom, middle, top
        for layerCells in layers:
            for word, high, logical in layerCells:
                words.append(word)
                ext.append(high & (3 << EXT_HIGH_SHIFT))
                refs.append(logical)
    return words, ext if useExt else None, refs if oldRows is not None else None


def sheets_for(mfile, tilesets, pairs, anims, callbacks):
    """Every (primary, secondary) sheet pairing this metatile file is drawn with."""
    users = [t for t in tilesets.values() if t['metatiles'] == mfile.path]
    result = []
    for t in users:
        partners = [b if not t['isSecondary'] else a for a, b in pairs if (a if not t['isSecondary'] else b) == t['symbol']]
        if not partners:
            partners = [None]
        for partner in sorted(set(partners), key=str):
            other = tilesets.get(partner) if partner else None
            primary, secondary = (other, t) if t['isSecondary'] else (t, other)
            anim = set()
            for owner in (primary, secondary):
                if owner is None:
                    # Without the partner its animations are unknown, so treat every slot as animated.
                    anim |= set(range(LEGACY_TOTAL_TILES))
                elif callbacks.get(owner['symbol']):
                    anim |= anims.get(callbacks[owner['symbol']], set())
            result.append(Sheets(primary, secondary, frozenset(anim)))
    return result


def layout_pairs():
    return {(l['primary_tileset'], l['secondary_tileset']) for l in load_layouts()}


def metatile_files(tilesets):
    files = {}
    for t in tilesets.values():
        if t['metatiles']:
            if not t['attributes']:
                fail('%s has metatiles but no attributes' % t['symbol'])
            files.setdefault(t['metatiles'], MetatileFile(t['metatiles'], t['attributes']))
    on_disk = set()
    for dirpath, _, names in os.walk(path('data/tilesets')):
        if 'metatiles.bin' in names:
            on_disk.add(os.path.relpath(os.path.join(dirpath, 'metatiles.bin'), ROOT))
    orphans = on_disk - set(files)
    if orphans:
        fail('metatiles.bin not referenced by src/data/tilesets/metatiles.h: %s' % ', '.join(sorted(orphans)))
    return files


def cmd_analyze(args, write=False):
    tilesets = project_tilesets()
    files = metatile_files(tilesets)
    formats = {f.format for f in files.values()}
    if formats == {'new'}:
        if write:
            fail('every tileset is already in the twelve-subtile format; nothing was changed')
        print('all %d metatile files are already twelve-subtile' % len(files))
        return
    if formats != {'old'}:
        mixed = sorted(f.path for f in files.values() if f.format == 'new')
        fail('some metatile files are already converted and others are not (%s); restore a consistent tree first'
             % ', '.join(mixed))
    for f in files.values():
        f.validate_old()
    pairs = layout_pairs()
    anims, callbacks = tileset_animation_slots(), tileset_callbacks()
    totals, outputs = {}, []
    for f in sorted(files.values(), key=lambda f: f.path):
        stats = {}
        words, ext, refs = convert(f, sheets_for(f, tilesets, pairs, anims, callbacks), stats)
        outputs.append((f, words, ext, refs))
        for k, v in stats.items():
            totals[k] = totals.get(k, 0) + v
        if args.verbose:
            print('%s: %d metatiles %s' % (f.path, f.count, stats))
    print('%d metatile files, %d metatiles: %s' % (len(files), sum(f.count for f in files.values()), totals))
    print('sidecars: %d ext, %d third-layer, %d palette_refs'
          % (sum(f.ext is not None for f in files.values()), sum(f.third is not None for f in files.values()),
             sum(f.refs is not None for f in files.values())))
    if not write:
        return
    for f, words, ext, refs in outputs:
        data = pack_words(words)
        write_atomic(path(f.path), data)
        if ext is not None:
            write_atomic(path(f.extPath), bytes(ext))
        if refs is not None:
            rows = [refs[i:i + NEW_WORDS] for i in range(0, len(refs), NEW_WORDS)]
            doc = {'version': 1, 'metatiles_sha256': hashlib.sha256(data).hexdigest(), 'palettes': rows}
            text = json.dumps(doc, indent=4) + '\n'
            tmp = path(f.refsPath) + '.migrating'
            open(tmp, 'w').write(text)
            os.replace(tmp, path(f.refsPath))
        for obsolete in (f.thirdPath, f.thirdExtPath):
            if os.path.exists(path(obsolete)):
                os.remove(path(obsolete))
    print('migrated %d metatile files to the twelve-subtile format' % len(outputs))


def cmd_migrate(args):
    cmd_analyze(args, write=True)


# ---------------------------------------------------------------------------------------------
# Verification


def logical_palette_file(tilesets, primary, secondary, logical, manifest):
    frlg = (primary or secondary or {}).get('frlg', False)
    split = PRIMARY_PALS_FRLG if frlg else PRIMARY_PALS
    if logical >= 14:
        entry = manifest.get(secondary['symbol']) if secondary else None
        if not entry:
            return None
        return entry['palettes'][logical - 14]['day']
    owner = primary if logical < split else secondary
    if owner is None or logical >= len(owner['palettes']):
        return None
    return owner['palettes'][logical]


class RootAt:
    def __init__(self, root):
        global ROOT
        self.saved = ROOT
        self.root = root

    def __enter__(self):
        global ROOT
        ROOT = self.root
        return self

    def __exit__(self, *exc):
        global ROOT
        ROOT = self.saved


def metatile_cells(mfile, index):
    """[(bottom, middle, top)] of (word, ext, logical) for one metatile of either format."""
    if mfile.format == 'old':
        words, ext, third, thirdExt = mfile.old_metatile(index)
        rows = mfile.refs['palettes'][index] if mfile.refs else None
        cells = old_placement(mfile.layer_type(index), words, ext, third, thirdExt, rows)
        return [((NORMAL_FILLER, 0, NORMAL_FILLER >> 12) if b is None else b, m, t) for b, m, t in cells]
    words = mfile.words[index * NEW_WORDS:(index + 1) * NEW_WORDS]
    ext = list(mfile.ext[index * NEW_WORDS:(index + 1) * NEW_WORDS]) if mfile.ext else [0] * NEW_WORDS
    rows = mfile.refs['palettes'][index] if mfile.refs else [w >> 12 for w in words]
    return [tuple((words[layer * 4 + i], ext[layer * 4 + i], rows[layer * 4 + i]) for layer in range(3)) for i in range(4)]


def cell_pixels(sheets, entry):
    tile = sheets.tile(virtual(entry[0], entry[1]))
    if tile is None:
        tile = np.zeros((8, 8), np.uint8)
    return oriented(tile, entry[0])


def snapshot(root):
    """Everything verify needs from one tree, loaded while ROOT points at it."""
    with RootAt(root):
        tilesets = project_tilesets()
        files = metatile_files(tilesets)
        manifest = palette_manifest()
        return tilesets, files, manifest, layout_pairs()


def cmd_verify(args):
    baseline = os.path.abspath(args.baseline)
    if not os.path.isdir(os.path.join(baseline, 'data/tilesets')):
        fail('%s is not a checkout of the pre-migration tree' % args.baseline)
    verify_trees(snapshot(baseline), snapshot(ROOT), baseline, args)


def verify_trees(old, new, baseline, args):
    oldTilesets, oldFiles, oldManifest, oldPairs = old
    newTilesets, newFiles, newManifest, newPairs = new
    problems = []
    if set(oldTilesets) - set(newTilesets):
        problems.append('tilesets disappeared: %s' % ', '.join(sorted(set(oldTilesets) - set(newTilesets))))
    for f in newFiles.values():
        if f.format != 'new':
            problems.append('%s is not twelve-subtile' % f.path)
    for key in sorted(oldFiles):
        o, n = oldFiles[key], newFiles.get(key)
        if n is None:
            problems.append('%s disappeared' % key)
            continue
        if o.count != n.count or o.attributes != n.attributes:
            problems.append('%s: metatile count or attributes changed' % key)
        if os.path.exists(path(n.thirdPath)) or os.path.exists(path(n.thirdExtPath)):
            problems.append('%s: third-layer files still present' % key)
    pairs = sorted(oldPairs | {(t['symbol'], None) for t in oldTilesets.values() if not t['isSecondary']}
                   | {(None, t['symbol']) for t in oldTilesets.values() if t['isSecondary']}, key=str)
    checked = cells = 0
    for primarySym, secondarySym in pairs:
        with RootAt(baseline):
            oldP, oldS = oldTilesets.get(primarySym), oldTilesets.get(secondarySym)
            oldSheets = Sheets(oldP, oldS)
            oldPal = palette_lookup(oldTilesets, oldP, oldS, oldManifest)
        newP, newS = newTilesets.get(primarySym), newTilesets.get(secondarySym)
        newSheets = Sheets(newP, newS)
        newPal = palette_lookup(newTilesets, newP, newS, newManifest)
        for owner in (oldP, oldS):
            if owner is None or not owner['metatiles']:
                continue
            o, n = oldFiles[owner['metatiles']], newFiles[owner['metatiles']]
            for index in range(o.count):
                before, after = metatile_cells(o, index), metatile_cells(n, index)
                for i in range(4):
                    cells += 1
                    issue = compare_cell(before[i], after[i], oldSheets, newSheets, oldPal, newPal)
                    if issue:
                        problems.append('%s+%s %s metatile %d cell %d: %s'
                                        % (primarySym, secondarySym, owner['symbol'], index, i, issue))
                        if len(problems) > 50:
                            break
            checked += 1
    for p in problems[:50]:
        print('MISMATCH ' + p)
    print('verified %d tileset pairings, %d metatile cells: %d mismatches' % (len(pairs), cells, len(problems)))
    sys.exit(1 if problems else 0)


def palette_lookup(tilesets, primary, secondary, manifest):
    """Colours of each logical palette, loaded now so the lookup works after ROOT changes."""
    table = {}
    for logical in range(18):
        file = logical_palette_file(tilesets, primary, secondary, logical, manifest)
        table[logical] = load_jasc(file) if file and os.path.exists(path(file)) else None
    return table.get


def compare_cell(before, after, oldSheets, newSheets, oldPal, newPal):
    def colours(sheets, pal, entry):
        with_index = cell_pixels(sheets, entry)
        colours = pal(entry[2])
        rgb = np.zeros((8, 8, 3), np.int16) - 1 if colours is None else colours[with_index].astype(np.int16)
        rgb[with_index == 0] = -1
        return with_index, rgb
    for layer, name in ((2, 'top'), (1, 'middle')):
        b, a = before[layer], after[layer]
        if (b[0], virtual(b[0], b[1]), b[2]) != (a[0], virtual(a[0], a[1]), a[2]):
            return '%s entry %04X/%d/%d became %04X/%d/%d' % (name, b[0], b[1], b[2], a[0], a[1], a[2])
        if not np.array_equal(colours(oldSheets, oldPal, b)[1], colours(newSheets, newPal, a)[1]):
            return '%s pixels differ' % name
    midIdx, _ = colours(newSheets, newPal, after[1])
    topIdx, _ = colours(newSheets, newPal, after[2])
    visible = (midIdx == 0) & (topIdx == 0)
    _, bottomBefore = colours(oldSheets, oldPal, before[0])
    _, bottomAfter = colours(newSheets, newPal, after[0])
    if not np.array_equal(bottomBefore[visible], bottomAfter[visible]):
        return 'visible bottom pixels differ'
    if before[0] != (NORMAL_FILLER, 0, 3) and before[0][:2] != after[0][:2]:
        return 'bottom entry %04X became %04X' % (before[0][0], after[0][0])
    return None


# ---------------------------------------------------------------------------------------------
# Rendering


def cmd_render(args):
    tilesets = project_tilesets()
    files = metatile_files(tilesets)
    manifest = palette_manifest()
    layout = next((l for l in load_layouts() if l['id'] == args.layout or l['name'] == args.layout), None)
    if layout is None:
        fail('no layout %s' % args.layout)
    primary, secondary = tilesets[layout['primary_tileset']], tilesets[layout['secondary_tileset']]
    sheets = Sheets(primary, secondary)
    pal = palette_lookup(tilesets, primary, secondary, manifest)
    split = PRIMARY_METATILES_FRLG if layout.get('layout_version') == 'frlg' else PRIMARY_METATILES
    blocks = read_words(path(layout['blockdata_filepath']))
    w, h = layout['width'], layout['height']
    image = np.zeros((h * 16, w * 16, 3), np.uint8)
    backdrop = pal(0)[0] if pal(0) is not None else np.zeros(3, np.uint8)
    image[:] = backdrop
    for y in range(h):
        for x in range(w):
            metatile = blocks[x + y * w] & 0x7FF
            owner, local = (primary, metatile) if metatile < split else (secondary, metatile - split)
            mfile = files.get(owner['metatiles'])
            if mfile is None or local >= mfile.count:
                continue
            for i, cell in enumerate(metatile_cells(mfile, local)):
                oy, ox = y * 16 + (i // 2) * 8, x * 16 + (i % 2) * 8
                for entry in cell:
                    idx = cell_pixels(sheets, entry)
                    colours = pal(entry[2])
                    if colours is None:
                        continue
                    mask = idx != 0
                    image[oy:oy + 8, ox:ox + 8][mask] = colours[idx][mask]
    Image.fromarray(image).save(args.out)
    print('%s -> %s' % (layout['id'], args.out))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('analyze')
    p.add_argument('--verbose', action='store_true')
    p = sub.add_parser('migrate')
    p.add_argument('--verbose', action='store_true')
    p = sub.add_parser('verify')
    p.add_argument('--baseline', required=True,
                   help='directory or tar of the pre-migration tree (needs data/tilesets, src/data/tilesets)')
    p = sub.add_parser('render')
    p.add_argument('layout')
    p.add_argument('--out', required=True)
    args = parser.parse_args()
    try:
        {'analyze': cmd_analyze, 'migrate': cmd_migrate, 'verify': cmd_verify, 'render': cmd_render}[args.cmd](args)
    except (MigrationError, ValueError) as e:
        sys.exit('metatile_format: error: %s' % e)


if __name__ == '__main__':
    main()
