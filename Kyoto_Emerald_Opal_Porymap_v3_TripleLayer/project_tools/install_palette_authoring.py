#!/usr/bin/env python3
from pathlib import Path
import argparse, json, shutil, re

ap=argparse.ArgumentParser()
ap.add_argument('root', type=Path)
args=ap.parse_args()
root=args.root.resolve()
required=[root/'include/tile_cache.h', root/'include/fieldmap.h', root/'include/global.fieldmap.h']
missing=[str(p) for p in required if not p.exists()]
if missing:
    raise SystemExit('Missing Kyoto M1+M2 files: ' + ', '.join(missing))
field=(root/'include/fieldmap.h').read_text()
if not re.search(r'#define\s+NUM_TILES_PER_METATILE\s+8\b', field):
    raise SystemExit('Refusing: this installer targets Kyoto Emerald 8-subtile metatiles')
branch='' # git is checked by shell wrapper; keep this installer filesystem-only.
tool_dir=root/'tools/opal_map_palettes'
tool_dir.mkdir(parents=True, exist_ok=True)
src=Path(__file__).resolve().parent/'compiler.py'
shutil.copy2(src, tool_dir/'compiler.py')
manifest=root/'data/tilesets/opal_palettes.json'
if not manifest.exists():
    manifest.write_text(json.dumps({'version':1,'tilesets':[]}, indent=2)+'\n')
else:
    data=json.loads(manifest.read_text())
    if data.get('version') != 1 or not isinstance(data.get('tilesets'), list):
        raise SystemExit(f'Invalid existing manifest: {manifest}')
print('Installed Kyoto M3 palette authoring metadata/tools.')
print('Porymap will expose logical palettes 0..17 when opened with the v2 editor.')
print('No in-game M3 palette runtime was enabled by this authoring installer.')
