#!/usr/bin/env python3
"""Safely set Pokekyoto title song volume to V127; back up midi.cfg."""
import pathlib, re, shutil, datetime, sys
repo=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else '.').expanduser().resolve()
p=repo/'sound/songs/midi/midi.cfg'
if not p.is_file(): raise SystemExit('ERROR: open this from the root of your Pokekyoto source repository.')
s=p.read_text(encoding='utf-8')
m=re.findall(r'^mus_title\.mid:[^\n]*$',s,re.MULTILINE)
if len(m)!=1: raise SystemExit('ERROR: mus_title.mid config line missing/duplicated; no changes made.')
line=m[0]
if '-G_kyoto_title' not in line: raise SystemExit('ERROR: custom title song has not been installed; no changes made.')
if '-V127' in line:
    print('Volume is already V127; no change necessary.')
    sys.exit(0)
if not re.search(r'-V100\b',line): raise SystemExit('ERROR: unexpected volume; no changes made: '+line)
new_line=re.sub(r'-V100\b','-V127',line)
backup=repo/'.pokekyoto_title_music_backups'/('volume-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'))/'sound/songs/midi/midi.cfg'
backup.parent.mkdir(parents=True,exist_ok=True)
shutil.copy2(p,backup)
p.write_text(s.replace(line,new_line,1),encoding='utf-8')
print('Title song volume raised from V100 to V127 (+2.08 dB).')
print('Backup:',backup)
print('Now run: make -j4')
