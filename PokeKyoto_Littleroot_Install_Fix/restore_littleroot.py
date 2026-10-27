#!/usr/bin/env python3
"""Restore files changed by one Littleroot music installer run.
From repo root:
python3 PokeKyoto_Littleroot_HQ/restore_littleroot.py . .pokekyoto_littleroot_music_backups/<timestamp>
"""
from pathlib import Path
import argparse,json,shutil

def main():
 p=argparse.ArgumentParser()
 p.add_argument('repo')
 p.add_argument('backup')
 a=p.parse_args()
 root=Path(a.repo).expanduser().resolve(); b=Path(a.backup).expanduser()
 if not b.is_absolute():b=root/b
 b=b.resolve()
 if not b.is_relative_to(root/'.pokekyoto_littleroot_music_backups'):
  raise SystemExit('Refusing backup outside .pokekyoto_littleroot_music_backups')
 info=json.loads((b/'restore_manifest.json').read_text())
 for rel in info['originals_present']:
  dest=root/rel
  dest.parent.mkdir(parents=True,exist_ok=True)
  shutil.copy2(b/rel,dest)
  print('Restored:',rel)
 for rel in info['files_new']:
  dst=root/rel
  if dst.is_file():dst.unlink();print('Removed newly created:',rel)
 print('Restored prior project state; run make -j4 again.')
if __name__=='__main__':main()
