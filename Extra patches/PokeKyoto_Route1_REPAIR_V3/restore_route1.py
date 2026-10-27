#!/usr/bin/env python3
"""Restore a Route 1 install from an explicit backup folder.

Usage:
    python3 PokeKyoto_Route1_REPAIR_V3/restore_route1.py . \
        .pokekyoto_route1_backups/YYYYMMDD-HHMMSS-ffffff
"""
import json
import shutil
import sys
from pathlib import Path

def main():
    if len(sys.argv)!=3:
        print('Usage: restore_route1.py PROJECT BACKUP_FOLDER')
        return 1
    root=Path(sys.argv[1]).resolve()
    backup=Path(sys.argv[2]).resolve()
    expected=root/'.pokekyoto_route1_backups'
    if expected not in backup.parents or not (backup/'restore_manifest.json').is_file():
        print('ERROR: Specify an existing Route 1 backup folder inside this project')
        return 1
    manifest=json.loads((backup/'restore_manifest.json').read_text('utf-8'))
    for rel in manifest.get('restore',[]):
        d=root/rel
        d.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(backup/rel,d)
        print('RESTORED',rel)
    for rel in manifest.get('delete',[]):
        (root/rel).unlink(missing_ok=True)
        print('DELETED newly added file:',rel)
    print('Restore completed; rebuild with make -j4')
    return 0
if __name__=='__main__':
    raise SystemExit(main())
