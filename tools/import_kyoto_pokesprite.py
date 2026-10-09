#!/usr/bin/env python3
"""Import the user's legacy 40x30 PokéSprite icons, with their OWN palettes.

Usage: python3 tools/import_kyoto_pokesprite.py /path/to/msikma/pokesprite
Pillow is only required to regenerate assets, not to build the ROM.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def gba(rgb):
    return (rgb[0] >> 3) | ((rgb[1] >> 3) << 5) | ((rgb[2] >> 3) << 10)


def rgb(value):
    return tuple(((value >> shift) & 31) * 255 // 31 for shift in (0, 5, 10))


def convert(source, bounds):
    image = Image.open(source).convert('RGBA').crop(bounds)
    # Preserve EVERY source pixel, including the 27 designs wider than 32px.
    # A 32x32 OBJ plus an 8x32 subsprite displays this 40x32 canvas.
    assert image.width <= 40 and image.height <= 30, source
    canvas = Image.new('RGBA', (40, 32))
    canvas.paste(image, ((40 - image.width) // 2, 31 - image.height))
    values = [gba(p[:3]) if p[3] >= 128 else None for p in canvas.getdata()]
    colors = sorted(set(values) - {None})
    reduced = len(colors) > 15
    if reduced:
        # A few upstream files exceed the GBA's 15 opaque colors. Quantize
        # only those files, after reducing to the real hardware RGB555 range.
        opaque = [v for v in values if v is not None]
        strip = Image.new('RGB', (len(opaque), 1)); strip.putdata([rgb(v) for v in opaque])
        quant = strip.quantize(colors=15, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).convert('RGB')
        colors = sorted(set(gba(p) for p in quant.getdata()))
        mapping = {v: min(colors, key=lambda c: sum((a-b)**2 for a,b in zip(rgb(v),rgb(c)))) for v in set(opaque)}
        values = [mapping[v] if v is not None else None for v in values]
    palette = [0] + colors
    pixels = [0 if v is None else palette.index(v, 1) for v in values]
    preview = Image.new('P', (40, 32)); preview.putdata(pixels)
    preview.putpalette([c for v in palette for c in rgb(v)] + [0] * (768 - len(palette) * 3))
    preview.info['transparency'] = 0
    tiles = bytearray()
    # Tile order matches the two hardware OBJs: 16 main tiles, 4 side tiles.
    for txs in (range(4), range(4, 5)):
      for ty in range(4):
        for tx in txs:
            for y in range(8):
                for x in range(0, 8, 2):
                    i = (ty*8+y)*40 + tx*8+x
                    tiles.append(pixels[i] | pixels[i+1] << 4)
    packed = bytes(tiles) + struct.pack('<16H', *(palette + [0]*(16-len(palette))))
    return packed, preview, {'resized': False, 'source_bounds': bounds, 'canvas': [40, 32], 'reduced_palette': reduced, 'opaque_colors': len(colors)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    args = parser.parse_args()
    source = args.source / 'icons/pokemon'
    output = ROOT / 'graphics/pokemon/kyoto_pokesprite'
    output.mkdir(parents=True, exist_ok=True)
    species_text = (ROOT / 'include/constants/species.h').read_text()
    entries = re.findall(r'\b(SPECIES_[A-Z0-9_]+)\s*=\s*(\d+)\s*,', species_text)
    all_names = set(re.findall(r'\bSPECIES_[A-Z0-9_]+\b', species_text))
    files = {p.stem: p for p in (source/'regular').glob('*.png')}
    normalized = {re.sub('[^a-z0-9]', '', name): name for name in files}
    aliases = {
        'CASTFORM_NORMAL': 'castform', 'MOTHIM_PLANT': 'mothim', 'SCATTERBUG_ICY_SNOW': 'scatterbug', 'SPEWPA_ICY_SNOW': 'spewpa', 'XERNEAS_NEUTRAL': 'xerneas', 'ZYGARDE_50': 'zygarde', 'HOOPA_CONFINED': 'hoopa',
        'DEOXYS_NORMAL': 'deoxys', 'WORMADAM_PLANT': 'wormadam',
        'BURMY_PLANT': 'burmy', 'CHERRIM_OVERCAST': 'cherrim',
        'SHELLOS_WEST': 'shellos', 'GASTRODON_WEST': 'gastrodon',
        'GIRATINA_ALTERED': 'giratina', 'SHAYMIN_LAND': 'shaymin',
        'ARCEUS_NORMAL': 'arceus', 'DARMANITAN_STANDARD': 'darmanitan',
        'DARMANITAN_GALAR_STANDARD': 'darmanitan-galar',
        'TORNADUS_INCARNATE': 'tornadus', 'THUNDURUS_INCARNATE': 'thundurus',
        'LANDORUS_INCARNATE': 'landorus', 'KELDEO_ORDINARY': 'keldeo',
        'MELOETTA_ARIA': 'meloetta', 'AEGISLASH_SHIELD': 'aegislash',
        'MEOWSTIC_M': 'meowstic', 'MEOWSTIC_F': 'female/meowstic',
        'FLABEBE_RED': 'flabebe', 'FLOETTE_RED': 'floette', 'FLORGES_RED': 'florges',
        'BASCULIN_RED_STRIPED': 'basculin', 'DEERLING_SPRING': 'deerling',
        'SAWSBUCK_SPRING': 'sawsbuck', 'FURFROU_NATURAL': 'furfrou',
        'PUMPKABOO_AVERAGE': 'pumpkaboo', 'GOURGEIST_AVERAGE': 'gourgeist',
        'ZYGARDE_50_AURA_BREAK': 'zygarde', 'ZYGARDE_50_POWER_CONSTRUCT': 'zygarde',
        'ZYGARDE_10_AURA_BREAK': 'zygarde-10', 'ZYGARDE_10_POWER_CONSTRUCT': 'zygarde-10',
        'ORICORIO_BAILE': 'oricorio', 'LYCANROC_MIDDAY': 'lycanroc',
        'WISHIWASHI_SOLO': 'wishiwashi', 'SILVALLY_NORMAL': 'silvally',
        'MINIOR_METEOR_RED': 'minior', 'MIMIKYU_DISGUISED': 'mimikyu',
        'MIMIKYU_BUSTED': 'mimikyu', 'GRENINJA_BOND': 'greninja',
        'ROCKRUFF_OWN_TEMPO': 'rockruff', 'NECROZMA_DUSK_MANE': 'necrozma-dusk',
        'NECROZMA_DAWN_WINGS': 'necrozma-dawn', 'UNOWN_A': 'unown',
    }
    for name in all_names:
        key = name[8:]
        if key.startswith('MINIOR_METEOR_'): aliases[key] = 'minior'
        if key.startswith('MINIOR_CORE_'): aliases[key] = 'minior-' + key[12:].lower()
    mapped = {}
    for symbol, number in entries:
        key = symbol[8:]
        slug = aliases.get(key, normalized.get(re.sub('[^a-z0-9]', '', key.lower())))
        if slug and (source/'regular'/f'{slug}.png').exists():
            mapped[symbol] = slug
    # Explicitly assert that no base Gen 1-7 species silently falls back.
    missing_base = [s for s,n in entries if 1 <= int(n) <= 807 and s not in mapped]
    if missing_base:
        raise SystemExit('Missing base species: ' + ', '.join(missing_base))
    used = sorted(set(mapped.values()))
    for slug in list(used):
        if (source/'regular/female'/f'{slug}.png').exists(): used.append('female/'+slug)
    used = sorted(set(used))
    blob = bytearray(1344)  # index 0 means no imported icon
    manifest = {'upstream': 'https://github.com/msikma/pokesprite',
                'commit': subprocess.check_output(['git','-C',str(args.source),'rev-parse','HEAD'],text=True).strip(),
                'source_directory': 'icons/pokemon', 'assets': {}, 'species': mapped}
    ids = {}
    for i, slug in enumerate(used, 1):
        ids[slug] = i
        regular = source/'regular'/f'{slug}.png'; shiny = source/'shiny'/f'{slug}.png'
        if not shiny.exists(): raise SystemExit(f'Missing shiny: {slug}')
        a = Image.open(regular).convert('RGBA').getbbox(); b = Image.open(shiny).convert('RGBA').getbbox()
        bounds = (min(a[0],b[0]),min(a[1],b[1]),max(a[2],b[2]),max(a[3],b[3]))
        report = {}
        for variant, path in [('regular',regular),('shiny',shiny)]:
            data, preview, detail = convert(path,bounds); blob.extend(data)
            dest = output/variant/f'{slug}.png';dest.parent.mkdir(parents=True,exist_ok=True);preview.save(dest)
            detail['source_sha256'] = hashlib.sha256(path.read_bytes()).hexdigest();report[variant] = detail
        manifest['assets'][slug] = report
    (output/'icons.bin').write_bytes(blob)
    # Multi-select is a native 8bpp BG overlay, shared by up to 30 icons.
    # Give it a 96-color palette derived from these same imported images.
    bulk_colors = []
    for offset in range(1344, len(blob), 672):
        pal = struct.unpack('<16H', blob[offset+640:offset+672])
        for byte in blob[offset:offset+640]:
            for index in (byte & 15, byte >> 4):
                if index: bulk_colors.append(rgb(pal[index]))
    strip = Image.new('RGB', (len(bulk_colors), 1)); strip.putdata(bulk_colors)
    bulk = strip.quantize(colors=96, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).getpalette()[:288]
    (output/'bulk_palette.bin').write_bytes(struct.pack('<96H', *(gba(bulk[i:i+3]) for i in range(0,288,3))))
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    table = ['// Generated by tools/import_kyoto_pokesprite.py. Zero entries use native Kyoto icons.',
             'static const u16 sPokespriteIds[NUM_SPECIES][2] = {']
    for species, slug in mapped.items():
        table.append(f'    [{species}] = {{{ids[slug]}, {ids.get("female/"+slug,ids[slug])}}},')
    table.append('};\n')
    (ROOT/'src/data/kyoto_pokesprite.h').write_text('\n'.join(table))
    shutil.copyfile(args.source/'license.md',output/'LICENSE.txt')
    shutil.copyfile(args.source/'contributors.md',output/'CONTRIBUTORS.md')
    print(f'{len(mapped)} species/forms; {len(used)} icon designs, each regular + shiny; {len(blob)} bytes')


if __name__ == '__main__':
    main()
