#!/usr/bin/env python3
"""Safely add the louder full-orchestra Town.mp3 as a dedicated Kyoto song for Littleroot Town.

Usage from the Pokekyoto source directory:
  python3 PokeKyoto_Littleroot_Install_Fix/install_littleroot.py . --dry-run
  python3 PokeKyoto_Littleroot_Install_Fix/install_littleroot.py .

Louder V2: preserves the 13379 Hz global mixer; compresses only this PCM sample.
"""
from __future__ import annotations
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import sys
import wave

SONG='mus_kyoto_littleroot'
MUSIC='MUS_KYOTO_LITTLEROOT'
SAMPLE='kyoto_littleroot'
VOICE_REF='.include "sound/voicegroups/kyoto_littleroot.inc"'
DIRECT_REF='DirectSoundWaveData_kyoto_littleroot::'
MAP='data/maps/LittlerootTown/map.json'
CONFIG='mus_kyoto_littleroot.mid:        -E -R0 -G_kyoto_littleroot -V127'
VOICE='''@ PokeKyoto Littleroot original orchestra from Town.mp3. NO MIDI reconstruction.
@ Program 0, key C4.  Original GBA wave data loops in hardware's sound mixer.
@ Use voice_directsound (resampling enabled) so 13k/26k mixer choices stay in tune.
voice_group kyoto_littleroot
    voice_directsound 60, 0, DirectSoundWaveData_kyoto_littleroot, 255, 255, 255, 0
'''
DIRECT='''\n\t.align 2
DirectSoundWaveData_kyoto_littleroot::
\t.incbin "sound/direct_sound_samples/kyoto_littleroot.bin"
'''
KNOWN_CUSTOM_PCM={
    'sound/voicegroups/kyoto_title.inc':'DirectSoundWaveData_kyoto_title',
    'sound/voicegroups/kyoto_route1.inc':'DirectSoundWaveData_kyoto_route1',
}

class Refuse(Exception):
    pass

def refuse(msg):
    raise Refuse(msg)

def hash_of(content):
    return hashlib.sha256(content).hexdigest()

def read_text(path):
    return path.read_text(encoding='utf-8')

def patch(root, asset_root, quality, assign, force_map):
    required=['Makefile','src/m4a.c','sound/song_table.inc','sound/songs/midi/midi.cfg',
              'include/constants/songs.h','sound/voice_groups.inc','sound/direct_sound_data.inc',MAP]
    absent=[p for p in required if not (root/p).is_file()]
    if absent: refuse('This is not the expected PokeKyoto source tree. Missing: '+', '.join(absent))
    mfpath=asset_root.parent/'manifest.json'
    if not mfpath.is_file(): refuse('Package manifest missing.')
    manifest=json.loads(mfpath.read_text(encoding='utf-8'))
    q=manifest['versions'][quality]
    rate=q['rate']
    assets={
       f'{quality}/kyoto_littleroot.wav':'sound/direct_sound_samples/kyoto_littleroot.wav',
       f'{quality}/kyoto_littleroot.bin':'sound/direct_sound_samples/kyoto_littleroot.bin',
       'mus_kyoto_littleroot.mid':'sound/songs/midi/mus_kyoto_littleroot.mid',
    }
    changes={}
    for src_rel,target in assets.items():
        inp=asset_root/src_rel
        if not inp.is_file():refuse(f'Missing packaged sample: {inp}')
        data=inp.read_bytes()
        key='assets/'+src_rel
        if hash_of(data)!=manifest['files'][key]['sha256']:
            refuse(f'Checksum mismatch: {inp}')
        changes[target]=data
    with wave.open(str(asset_root/quality/'kyoto_littleroot.wav'),'rb') as w:
        if (w.getnchannels(),w.getsampwidth(),w.getframerate())!=(1,2,rate):
            refuse('WAV invalid format; expected 16-bit mono PCM at chosen rate.')
        count=w.getnframes()
    bin_data=changes['sound/direct_sound_samples/kyoto_littleroot.bin']
    if struct.unpack('<4I',bin_data[:16])!=(0x40000000,rate*1024,0,count):
        refuse('GBA PCM wave header invalid or loop mismatch.')

    data={rel:read_text(root/rel) for rel in required if rel!='Makefile'}
    music_table=data['sound/song_table.inc']
    entries=re.findall(r'^\s*song\s+(\w+)\s*,\s*MUSIC_PLAYER_\w+\s*,\s*\d+',music_table,re.M)
    if not entries or entries[0]!='mus_dummy':refuse('Unexpected sound/song_table.inc layout')
    found=[i for i,song in enumerate(entries) if song==SONG]
    if len(found)>1: refuse('Duplicate mus_kyoto_littleroot entries. Please review.')
    if found:
        index=found[0]
        if f'song {SONG}, MUSIC_PLAYER_BGM, 0' not in music_table:
            refuse('Existing Kyoto Littleroot song header is unexpected.')
    else:
        index=len(entries)
        anchor='\n\t.align 2\ndummy_song_header:'
        if music_table.count(anchor)!=1:
            refuse('Expected song table insertion anchor not found exactly once.')
        music_table=music_table.replace(anchor,
            '\n\t@ Kyoto Littleroot Town independent BGM (append only)'
            f'\n\tsong {SONG}, MUSIC_PLAYER_BGM, 0'+anchor,1)
    changes['sound/song_table.inc']=music_table.encode()

    defs=data['include/constants/songs.h']
    dm=re.findall(r'^#define\s+'+MUSIC+r'\s+(\d+)\b',defs,re.M)
    if len(dm)>1: refuse('Duplicate Kyoto Littleroot music constant.')
    if dm:
        if int(dm[0])!=index:
            refuse(f'Song ID conflict: {MUSIC} is defined as {dm[0]}, table index is {index}.')
    else:
        anchor='#define MUS_ROUTE118'
        if defs.count(anchor)!=1: refuse('Cannot safely insert new song constant.')
        defs=defs.replace(anchor,f'#define {MUSIC:<30} {index} // Kyoto Littleroot Town full orchestral sample\n'+anchor,1)
    changes['include/constants/songs.h']=defs.encode()

    cfg=data['sound/songs/midi/midi.cfg']
    match=re.findall(r'^mus_kyoto_littleroot\.mid:[^\r\n]*$',cfg,re.M)
    if len(match)>1:refuse('Multiple MIDI-config entries for Kyoto Littleroot.')
    if match:
        if '-G_kyoto_littleroot' not in match[0]:
            refuse('An incompatible Kyoto Littleroot music config entry already exists.')
        # Only touch the target Kyoto Littleroot track, leaving all other songs intact.
        # Existing installation may use -V115; raise it to -V127 on user request.
        vol = re.search(r'(?<!\S)-V(\d{1,3})(?!\S)', match[0])
        if vol is None: refuse('Kyoto Littleroot MIDI configuration has no -V volume setting.')
        new_line = re.sub(r'(?<!\S)-V\d{1,3}(?!\S)', '-V127', match[0], count=1)
        if match[0] != new_line:
            cfg = cfg.replace(match[0], new_line, 1)
    else:
        cfg=cfg.rstrip()+'\n'+CONFIG+'\n'
    changes['sound/songs/midi/midi.cfg']=cfg.encode()

    group=data['sound/voice_groups.inc']
    if VOICE_REF not in group:
        group=group.rstrip()+'\n\n@ Kyoto Littleroot Town full sample\n'+VOICE_REF+'\n'
    elif group.count(VOICE_REF)!=1: refuse('Duplicate Kyoto Littleroot voice include.')
    changes['sound/voice_groups.inc']=group.encode()

    inc=data['sound/direct_sound_data.inc']
    if DIRECT_REF not in inc:
        inc=inc.rstrip()+DIRECT
    elif inc.count(DIRECT_REF)!=1 or '"sound/direct_sound_samples/kyoto_littleroot.bin"' not in inc:
        refuse('Existing Kyoto Littleroot PCM definition is inconsistent.')
    changes['sound/direct_sound_data.inc']=inc.encode()
    changes['sound/voicegroups/kyoto_littleroot.inc']=VOICE.encode()

    current_map=data[MAP]
    try:map_obj=json.loads(current_map)
    except json.JSONDecodeError:refuse('Littleroot map.json has invalid JSON.')
    if map_obj.get('id')!='MAP_LITTLEROOT_TOWN': refuse('Unexpected Littleroot map ID.')
    original_music=map_obj.get('music')
    if assign and original_music!=MUSIC:
        if original_music not in ('MUS_RG_CELADON','MUS_LITTLEROOT','MUS_LITTLEROOT_TEST') and not force_map:
            refuse(f'Littleroot currently uses {original_music} (not the inspected MUS_RG_CELADON). '
                   'No change made. To replace its current music explicitly, use --force-map-music.')
        new_map,n=re.subn(r'("music"\s*:\s*")[A-Z_0-9]+(")',
                 lambda m:m.group(1)+MUSIC+m.group(2),current_map,count=1)
        if n!=1:refuse('Cannot change Littleroot song safely')
        changes[MAP]=new_map.encode()

    mixer=data['src/m4a.c']
    expected='| SOUND_MODE_FREQ_13379'
    upgraded='| SOUND_MODE_FREQ_26758'
    if quality=='hq':
        if mixer.count(expected)==1 and upgraded not in mixer:
            mixer=mixer.replace(expected,upgraded,1)
        elif mixer.count(upgraded)!=1:
            refuse('Global GBA sound mixer is customized; stopping HQ upgrade to avoid unsafe changes.')
    else:
        # Explicitly selected compatible can return the mixer to 13,379 Hz.
        if mixer.count(upgraded)==1 and expected not in mixer:
            mixer=mixer.replace(upgraded,expected,1)
        elif mixer.count(expected)!=1:
            refuse('Mixer has an unexpected sample rate; stopping to protect audio.')
    changes['src/m4a.c']=mixer.encode()

    if quality=='hq':
        # Previously supplied full-song packs used the rate-locked (type 8) voice.
        # At 2x mixer rate it would play twice as fast. Their existing PCM wave
        # headers already store the original rate; the rate-aware (type 0) voice
        # preserves natural speed by resampling instead.
        for rel,marker in KNOWN_CUSTOM_PCM.items():
            p=root/rel
            if not p.is_file(): continue
            text=read_text(p)
            if marker not in text:
                refuse(f'Unexpected earlier PCM definition in {rel}. Cannot verify playback speed.')
            text,n=re.subn(r'\bvoice_directsound_no_resample\s+(?=60\s*,\s*0\s*,\s*'+re.escape(marker)+r'\s*,)',
                           'voice_directsound ',text)
            changes[rel]=text.encode()
        # The high-rate mixer could cause other type-8 voices to play too fast.
        # Refuse rather than silently modifying someone else's music.
        for f in sorted((root/'sound/voicegroups').rglob('*.inc')):
            rel=f.relative_to(root).as_posix()
            text=changes.get(rel)
            if text is None: text=f.read_bytes()
            if b'voice_directsound_no_resample' in text and rel not in KNOWN_CUSTOM_PCM:
                refuse(f'HQ audio may change playback speed in {rel} (no-resample instrument). '
                       'No changes made. Use --quality compatible or inspect this voice.')

    return changes,index,original_music,rate

def main():
    p=argparse.ArgumentParser(description='Install Town.mp3 full-orchestra sample on Littleroot Town in PokeKyoto')
    p.add_argument('repo',nargs='?',default='.',help='Path to existing Pokekyoto source checkout')
    p.add_argument('--quality',choices=['compatible'],default='compatible',
                   help='compatible 13379 Hz: keep original GBA mixer rate (safe profile).')
    p.add_argument('--dry-run',action='store_true',help='Check source and make a plan without any changes')
    p.add_argument('--no-assign',action='store_true',help='Register custom song without changing the Littleroot map')
    p.add_argument('--force-map-music',action='store_true',help='Overwrite unexpected custom map music explicitly')
    args=p.parse_args()
    root=Path(args.repo).expanduser().resolve()
    path=Path(__file__).resolve().parent
    try:
        changes,songid,previous,rate=patch(root,path/'assets',args.quality,not args.no_assign,args.force_map_music)
    except Refuse as e:
        raise SystemExit('STOP (no changes made): '+str(e))
    modified={rel:contents for rel,contents in changes.items()
              if not (root/rel).is_file() or (root/rel).read_bytes()!=contents}
    print(f'Project: {root}')
    print(f'Music: {MUSIC} (song ID {songid}, previous Littleroot: {previous})')
    print(f'Audio: {rate} Hz, PCM8 mono, full original orchestral recording')
    print(f'Profile: {args.quality}')
    if args.quality=='hq':
        print('IMPORTANT: HQ increases the GLOBAL mixer rate to 26758 Hz. '+
              'Test FPS, title music, sound effects, weather and map transitions in mGBA.')
    else:print('Compatible profile keeps the original GBA 13379 Hz mixer rate.')
    print('Files to create/change:')
    for rel in modified: print(' -',rel)
    if not modified:
        print('Already installed: no changes needed.')
        return 0
    if args.dry_run:
        print('DRY RUN OK: files untouched.')
        return 0
    backup=root/'.pokekyoto_littleroot_music_backups'/datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    originals=[]
    for rel in modified:
        old=root/rel
        if old.is_file():
            dest=backup/rel
            dest.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(old,dest)
            originals.append(rel)
    backup.mkdir(parents=True,exist_ok=True)
    (backup/'restore_manifest.json').write_text(json.dumps({
        'id':MUSIC,'song_index':songid,'quality':args.quality,
        'originals_present':originals,
        'files_new':[rel for rel in modified if rel not in originals],
        'files_modified':list(modified)
    },indent=2)+'\n',encoding='utf-8')
    try:
        for rel,body in modified.items():
            dest=root/rel
            dest.parent.mkdir(parents=True,exist_ok=True)
            dest.write_bytes(body)
    except Exception as exc:
        print('ERROR during installation. Restoring previous files...',file=sys.stderr)
        for rel in originals:
            dest=root/rel
            shutil.copy2(backup/rel,dest)
        for rel in modified:
            if rel not in originals:(root/rel).unlink(missing_ok=True)
        raise SystemExit(str(exc))
    print('\nInstalled. Original files backed up at:\n ',backup)
    print('Next: make -j4')
    print('IMPORTANT: actual game playback still needs a ROM build + mGBA verification.')
    return 0

if __name__=='__main__':
    sys.exit(main())
