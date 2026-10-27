#!/usr/bin/env python3
"""Restore only the newest PCM V4 installation backup."""
from __future__ import annotations
import argparse,json,shutil
from pathlib import Path

def main():
    p=argparse.ArgumentParser()
    p.add_argument('project',nargs='?',default='.')
    p.add_argument('--latest',action='store_true',required=True)
    args=p.parse_args()
    root=Path(args.project).resolve()
    base=root/'.pokekyoto_route1_pcm_v4_backups'
    all_backups=sorted(x for x in base.glob('*') if x.is_dir() and (x/'manifest.json').is_file()) if base.is_dir() else []
    if not all_backups:
        print('No PCM V4 backup available; nothing changed.')
        return 1
    bak=all_backups[-1]
    data=json.loads((bak/'manifest.json').read_text('utf-8'))
    for rel in data['restore']:
        dest=root/rel;dest.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(bak/rel,dest)
    for rel in data['delete']:
        (root/rel).unlink(missing_ok=True)
    print('Restored backup:',bak)
    print('Rebuild using make -j4 to restore the old ROM soundtrack.')
    return 0
if __name__=='__main__':
    raise SystemExit(main())
