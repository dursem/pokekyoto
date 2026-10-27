#!/usr/bin/env python3
"""Install the pre-rendered Kyoto title theme into a pokeemerald-expansion checkout.

Usage, from the Pokekyoto repository root:
    python3 Pokekyoto_TitleMusic/install_title_music.py .

The script has no external Python dependencies and does not build the ROM.
It creates a timestamped backup before modifying existing files.
"""
from __future__ import annotations
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys
import wave

ID='POKEKYOTO_TITLE_PCM_2026'
CFG_LINE='mus_title.mid:                 -E -R0 -G_kyoto_title -V100'
VOICE_LINE='.include "sound/voicegroups/kyoto_title.inc"'
SAMPLE_BLOCK='\n\t.align 2\nDirectSoundWaveData_kyoto_title::\n\t.incbin "sound/direct_sound_samples/kyoto_title.bin"\n'
VOICE_SRC='''@ PokeKyoto original-orchestration title theme from a full-song PCM sample.
@ MIDI program 0, note C4 (60). The sample itself loops; the sequencer holds it.
voice_group kyoto_title
    voice_directsound_no_resample 60, 0, DirectSoundWaveData_kyoto_title, 255, 255, 255, 0
'''

def sha256(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    a=argparse.ArgumentParser(description='Install a full orchestral audio sample as the MUS_TITLE song in Pokekyoto.')
    a.add_argument('repo',nargs='?',default='.',help='Root of your Pokekyoto decomp repository')
    a.add_argument('--dry-run',action='store_true',help='Validate without modifying files')
    args=a.parse_args()
    project=Path(args.repo).expanduser().resolve()
    scriptdir=Path(__file__).resolve().parent
    assets=scriptdir/'assets'

    required=['sound/songs/midi/mus_title.mid','sound/songs/midi/midi.cfg',
              'sound/voice_groups.inc','sound/direct_sound_data.inc',
              'src/title_screen.c','Makefile']
    absent=[x for x in required if not (project/x).is_file()]
    if absent:
        raise SystemExit('This is not the expected Pokekyoto decomp root. Missing: '+', '.join(absent))
    cfg=(project/'sound/songs/midi/midi.cfg').read_text(encoding='utf-8')
    matches=re.findall(r'^mus_title\.mid:[^\n]*$',cfg,flags=re.MULTILINE)
    if len(matches)!=1:
        raise SystemExit('Expected exactly one mus_title.mid entry in sound/songs/midi/midi.cfg')
    if 'm4aSongNumStart(MUS_TITLE)' not in (project/'src/title_screen.c').read_text(encoding='utf-8'):
        raise SystemExit('Title screen code differs from the inspected GitHub version; stopping for safety.')
    inc=(project/'sound/voice_groups.inc').read_text(encoding='utf-8')
    data=(project/'sound/direct_sound_data.inc').read_text(encoding='utf-8')
    if VOICE_LINE in inc and 'DirectSoundWaveData_kyoto_title::' not in data:
        raise SystemExit('Incomplete prior installation detected. Review sound includes before retrying.')
    if 'DirectSoundWaveData_kyoto_title::' in data and SAMPLE_BLOCK.strip() not in data:
        raise SystemExit('Different kyoto title sample definition already present; refusing to overwrite it.')
    midi=assets/'mus_title.mid'
    wav=assets/'kyoto_title.wav'
    binpath=assets/'kyoto_title.bin'
    for src in [midi,wav,binpath]:
        if not src.is_file() or src.stat().st_size<100:
            raise SystemExit('Missing/corrupt installer asset: '+str(src))
    with wave.open(str(wav),'rb') as w:
        if (w.getnchannels(),w.getsampwidth(),w.getframerate())!=(1,1,13379):
            raise SystemExit('Unexpected WAV format; refusing to install')
    manifest=json.loads((scriptdir/'audio_info.json').read_text(encoding='utf-8'))
    if wav.stat().st_size!=manifest['wav_bytes'] or binpath.stat().st_size!=manifest['agb_bin_bytes']:
        raise SystemExit('Unexpected asset size; refusing to install')

    voice=(project/'sound/voicegroups/kyoto_title.inc')
    wavdst=(project/'sound/direct_sound_samples/kyoto_title.wav')
    bindst=(project/'sound/direct_sound_samples/kyoto_title.bin')
    changes={
        'sound/songs/midi/mus_title.mid':midi.read_bytes(),
        'sound/songs/midi/midi.cfg':cfg.replace(matches[0],CFG_LINE,1).encode('utf-8'),
        'sound/voice_groups.inc':(inc if VOICE_LINE in inc else inc.rstrip()+ '\n\n@ Pokekyoto title theme\n'+VOICE_LINE+'\n').encode('utf-8'),
        'sound/direct_sound_data.inc':(data if 'DirectSoundWaveData_kyoto_title::' in data else data.rstrip()+'\n'+SAMPLE_BLOCK).encode('utf-8'),
        'sound/voicegroups/kyoto_title.inc':VOICE_SRC.encode('utf-8'),
        'sound/direct_sound_samples/kyoto_title.wav':wav.read_bytes(),
        'sound/direct_sound_samples/kyoto_title.bin':binpath.read_bytes(),
    }
    modified={rel:contents for rel,contents in changes.items() if not (project/rel).exists() or (project/rel).read_bytes()!=contents}
    if not modified:
        print('Everything is already installed. No files changed.')
        return
    print('Repo: '+str(project))
    print('Files to add/change:')
    for rel in modified: print('  '+rel)
    if args.dry_run:
        print('DRY RUN: no changes made.')
        return
    backupdir=project/'.pokekyoto_title_music_backups'/datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    backed=[]
    for rel in modified:
        src=project/rel
        if src.is_file():
            tgt=backupdir/rel
            tgt.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(src,tgt)
            backed.append(rel)
    record={'installation_id':ID,'modified':list(modified),'backed_up_existing':backed,
            'originals_absent':[rel for rel in modified if rel not in backed]}
    backupdir.mkdir(parents=True,exist_ok=True)
    (backupdir/'restore_manifest.json').write_text(json.dumps(record,indent=2),encoding='utf-8')
    for rel,contents in modified.items():
        dest=project/rel
        dest.parent.mkdir(parents=True,exist_ok=True)
        dest.write_bytes(contents)
    print('\nInstalled. Original versions backed up to:\n  '+str(backupdir))
    print('Now compile in this same directory:\n  make -j4')
    print('Then open pokeemerald.gba (or your configured ROM filename) in mGBA.')

if __name__=='__main__':
    main()
