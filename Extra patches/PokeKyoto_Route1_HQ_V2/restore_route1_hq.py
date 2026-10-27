#!/usr/bin/env python3
"""Restore a previous Route1 HQ update with its backup folder."""
from pathlib import Path
import argparse,json,shutil
p=argparse.ArgumentParser()
p.add_argument('repo',help='Your pokekyoto source directory')
p.add_argument('backup',help='Path printed by installer, absolute or relative to repo')
a=p.parse_args()
root=Path(a.repo).expanduser().resolve()
backup=Path(a.backup).expanduser()
if not backup.is_absolute():backup=root/backup
backup=backup.resolve()
allowed=root/'.pokekyoto_route1_hq_backups'
if not backup.is_relative_to(allowed) or not (backup/'restore_manifest.json').is_file():
    raise SystemExit('Invalid Route1 HQ backup location')
meta=json.loads((backup/'restore_manifest.json').read_text())
for rel in meta['originals_present']:
    dst=root/rel;dst.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(backup/rel,dst);print('Restored:',rel)
for rel in meta['files_new']:
    dst=root/rel
    if dst.is_file():dst.unlink();print('Removed installed file:',rel)
print('Restore complete. Run make -j4 to rebuild.')
