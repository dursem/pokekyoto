#!/usr/bin/env python3
"""Update old Route 1 PCM song to HQ without changing Littleroot/Title/Route 2.

Usage (from ~/PokemonDev/pokekyoto):
  python3 PokeKyoto_Route1_HQ_V2/install_route1_hq.py . --dry-run
  python3 PokeKyoto_Route1_HQ_V2/install_route1_hq.py . --quality hq
  python3 PokeKyoto_Route1_HQ_V2/install_route1_hq.py . --quality compatible
"""
from __future__ import annotations
import argparse, hashlib, json, re, shutil, struct, sys, wave
from datetime import datetime
from pathlib import Path

SONG='mus_kyoto_route1'
MUSIC='MUS_KYOTO_ROUTE1'
VOICE_INCLUDE='.include "sound/voicegroups/kyoto_route1.inc"'
DIRECT_SYMBOL='DirectSoundWaveData_kyoto_route1::'
DIRECT='''\n\t.align 2
DirectSoundWaveData_kyoto_route1::
\t.incbin "sound/direct_sound_samples/kyoto_route1.bin"
'''
VOICE='''@ PokeKyoto Route 1 HIGH QUALITY original orchestral PCM soundtrack.
@ Sample is original recorded mix, NOT MIDI note/instrument reconstruction.
@ C4 (60) directsound resampled from its wave header to the game's sample mixer.
voice_group kyoto_route1
    voice_directsound 60, 0, DirectSoundWaveData_kyoto_route1, 255, 255, 255, 0
'''
OLD_CONFIG='mus_kyoto_route1.mid:        -E -R0 -G_kyoto_route1 -V110'
NEW_CONFIG='mus_kyoto_route1.mid:        -E -R0 -G_kyoto_route1 -V115'
MAPS={
 'route1':('data/maps/Route1_Frlg/map.json','MAP_ROUTE1','MUS_RG_ROUTE1'),
 'route101':('data/maps/Route101/map.json','MAP_ROUTE101','MUS_ROUTE101'),
}
KNOWN_SAMPLE_VOICES={
 'sound/voicegroups/kyoto_title.inc':'DirectSoundWaveData_kyoto_title',
}
class Refuse(Exception):pass

def stop(msg): raise Refuse(msg)
def hash_file(path):
    sha=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):sha.update(block)
    return sha.hexdigest()

def load_asset(p,manifest,folder):
    if not p.is_file():stop(f'Package asset missing: {p}')
    name=p.relative_to(folder).as_posix()
    known=manifest.get('files',{}).get(name)
    if not known or hash_file(p)!=known['sha256'] or p.stat().st_size!=known['bytes']:
        stop(f'Package checksum mismatch: {name}')
    return p.read_bytes()

def build_changes(root,pkg,quality,target,assign,force_map):
    map_rel,map_id,original=MAPS[target]
    expected=['Makefile','src/m4a.c','sound/song_table.inc','sound/songs/midi/midi.cfg',
     'sound/voice_groups.inc','sound/direct_sound_data.inc','include/constants/songs.h',map_rel]
    missing=[s for s in expected if not (root/s).is_file()]
    if missing:stop('Not a PokeKyoto pokeemerald-expansion source checkout. Missing: '+', '.join(missing))
    manifest=json.loads((pkg/'manifest.json').read_text(encoding='utf-8'))
    q=manifest['versions'][quality]
    rate=q['rate'];n=q['samples']
    assets={f'assets/{quality}/kyoto_route1.wav':'sound/direct_sound_samples/kyoto_route1.wav',
            f'assets/{quality}/kyoto_route1.bin':'sound/direct_sound_samples/kyoto_route1.bin',
            'assets/mus_kyoto_route1.mid':'sound/songs/midi/mus_kyoto_route1.mid'}
    changes={dest:load_asset(pkg/src,manifest,pkg) for src,dest in assets.items()}
    with wave.open(str(pkg/f'assets/{quality}/kyoto_route1.wav'),'rb') as w:
        if (w.getnchannels(),w.getsampwidth(),w.getframerate(),w.getnframes())!=(1,2,rate,n):
            stop('Packaged WAV is not signed 16-bit mono at required rate/length.')
    bin_sample=changes['sound/direct_sound_samples/kyoto_route1.bin']
    if struct.unpack('<4I',bin_sample[:16])!=(0x40000000,rate*1024,0,n):
        stop('GBA audio wave header / loop metadata mismatch.')
    if len(bin_sample)!=(16+n+(-n)%4):stop('GBA audio sample byte count mismatch.')

    texts={rel:(root/rel).read_text(encoding='utf-8') for rel in expected if rel!='Makefile'}
    # Existing Route 1 custom song is reused (replaces previous bad 13 kHz audio).
    table=texts['sound/song_table.inc']
    entries=re.findall(r'^\s*song\s+(\w+)\s*,\s*MUSIC_PLAYER_\w+\s*,\s*\d+',table,re.M)
    if not entries or entries[0]!='mus_dummy' or len(entries)<610:
        stop('The song table is not the expected pokeemerald-expansion layout.')
    positions=[i for i,nm in enumerate(entries) if nm==SONG]
    if len(positions)>1:stop('Duplicate custom Route 1 song entries.')
    if positions:
        song_id=positions[0]
        if len(re.findall(r'^\s*song\s+'+SONG+r'\s*,\s*MUSIC_PLAYER_BGM\s*,\s*0\s*$',table,re.M))!=1:
            stop('Existing Kyoto Route 1 song table entry unexpectedly modified.')
    else:
        song_id=len(entries)
        match=re.search(r'\n\s*\.align 2\s*\ndummy_song_header:',table)
        if not match or 'dummy_song_header:' not in table:
            stop('Song table insertion point not found; refusing to renumber songs.')
        table=table[:match.start()]+'\n\t@ PokeKyoto original orchestra Route 1 music (appended)\n\tsong '+SONG+', MUSIC_PLAYER_BGM, 0'+table[match.start():]
    changes['sound/song_table.inc']=table.encode()

    defs=texts['include/constants/songs.h']
    found=re.findall(r'^#define\s+'+MUSIC+r'\s+(\d+)\b',defs,re.M)
    if len(found)>1:stop('Duplicate MUS_KYOTO_ROUTE1 constants.')
    if found:
        if int(found[0])!=song_id:stop(f'Route 1 music ID conflict: header {found[0]}, table {song_id}.')
    else:
        if defs.count('#define MUS_ROUTE118')!=1:stop('Cannot safely append custom music ID.')
        defs=defs.replace('#define MUS_ROUTE118',f'#define {MUSIC:<28} {song_id} // HQ original orchestral Route 1\n#define MUS_ROUTE118',1)
    changes['include/constants/songs.h']=defs.encode()

    cfg=texts['sound/songs/midi/midi.cfg']
    matches=re.findall(r'^mus_kyoto_route1\.mid:[^\n\r]*$',cfg,re.M)
    if len(matches)>1:stop('Duplicate Route 1 config entries.')
    if matches:
        if '-G_kyoto_route1' not in matches[0]:stop('Custom Route 1 config conflict.')
        # Upgrade only exact 2026-10-27 first patch value. User-set volumes remain untouched.
        if matches[0].strip()==OLD_CONFIG:
            cfg=cfg.replace(matches[0],NEW_CONFIG,1)
    else:
        cfg=cfg.rstrip()+'\n'+NEW_CONFIG+'\n'
    changes['sound/songs/midi/midi.cfg']=cfg.encode()

    groups=texts['sound/voice_groups.inc']
    if VOICE_INCLUDE not in groups:
        groups=groups.rstrip()+'\n\n@ PokeKyoto Route 1 HQ soundtrack\n'+VOICE_INCLUDE+'\n'
    elif groups.count(VOICE_INCLUDE)!=1:stop('Duplicate Route 1 voice group includes.')
    changes['sound/voice_groups.inc']=groups.encode()
    ds=texts['sound/direct_sound_data.inc']
    if DIRECT_SYMBOL not in ds:
        ds=ds.rstrip()+DIRECT
    elif ds.count(DIRECT_SYMBOL)!=1 or '"sound/direct_sound_samples/kyoto_route1.bin"' not in ds:
        stop('Unexpected Route 1 PCM sample definition; refusing to replace it.')
    changes['sound/direct_sound_data.inc']=ds.encode()
    # Resampled voice type (0) preserves pitch no matter whether mixer is 13k or 26k.
    changes['sound/voicegroups/kyoto_route1.inc']=VOICE.encode()

    m=texts[map_rel]
    try: js=json.loads(m)
    except ValueError: stop('Map JSON is invalid before install.')
    if js.get('id')!=map_id:stop(f'Wrong map selected: expected {map_id}, got {js.get("id")}.')
    old_music=js.get('music')
    if assign and old_music!=MUSIC:
        if old_music!=original and not force_map:
            stop(f'{map_rel} currently has a different song ({old_music}); no changes. '
                 'Use --force-map-music to replace its music intentionally.')
        replace,nchanges=re.subn(r'("music"\s*:\s*")[A-Z_0-9]+(")',
                  lambda match:match.group(1)+MUSIC+match.group(2),m,count=1)
        if nchanges!=1:stop('Could not replace the selected map music field.')
        m=replace
    changes[map_rel]=m.encode()

    mixer=texts['src/m4a.c']
    standard='| SOUND_MODE_FREQ_13379'
    high='| SOUND_MODE_FREQ_26758'
    if quality=='hq':
        if standard in mixer and high not in mixer:
            if mixer.count(standard)!=1:stop('Unexpected duplicate default mixer setup.')
            mixer=mixer.replace(standard,high,1)
            needs_mixer_upgrade=True
        elif mixer.count(high)==1: needs_mixer_upgrade=False
        else:stop('GBA mixer has an unexpected custom sample rate. Manual review required.')
    else:
        if mixer.count(high)!=1 and mixer.count(standard)!=1:
            stop('GBA mixer has an unexpected sample rate. Not changing sound code.')
        needs_mixer_upgrade=False
    changes['src/m4a.c']=mixer.encode()
    # HQ mixer needs sound tracks defined as no-resample to be converted to rate-aware voice.
    if needs_mixer_upgrade:
        for rel,voice in KNOWN_SAMPLE_VOICES.items():
            p=root/rel
            if not p.is_file():continue
            text=p.read_text(encoding='utf-8')
            if voice not in text:stop(f'Cannot confirm previous PCM voice in {rel}.')
            text,count=re.subn(r'\bvoice_directsound_no_resample\s+(?=60\s*,\s*0\s*,\s*'+re.escape(voice)+r'\s*,)',
                               'voice_directsound ',text)
            if count>1:stop(f'Unexpected multiple definitions in {rel}.')
            if count:changes[rel]=text.encode()
        for f in sorted((root/'sound/voicegroups').rglob('*.inc')):
            rel=f.relative_to(root).as_posix()
            if rel=='sound/voicegroups/kyoto_route1.inc':continue
            dat=changes.get(rel, f.read_bytes())
            if b'voice_directsound_no_resample' in dat:
                stop('Enabling the HQ mixer could change speed of another no-resample sample in '+rel+
                     '. No project files changed.')
    return changes,song_id,old_music,rate,map_rel,needs_mixer_upgrade

def main():
    parser=argparse.ArgumentParser(description='Replace the old Route 1 sample with high quality GBA PCM music')
    parser.add_argument('repo',nargs='?',default='.')
    parser.add_argument('--quality',choices=['hq','compatible'],default='hq',
                        help='HQ = 26758 Hz mixer and 26758 Hz PCM (recommended, increases CPU). Compatible = 13379 Hz PCM, leaves mixer as-is.')
    parser.add_argument('--target',choices=MAPS.keys(),default='route1',
                        help='route1 = Route1_Frlg (MAP_ROUTE1, default); route101 = Hoenn Route101.')
    parser.add_argument('--dry-run',action='store_true',help='Validate with no writes')
    parser.add_argument('--no-assign',action='store_true',help='Add song but leave map to assign manually in Porymap')
    parser.add_argument('--force-map-music',action='store_true',help='Explicitly allow replacing unexpected map music')
    args=parser.parse_args()
    root=Path(args.repo).expanduser().resolve();pkg=Path(__file__).resolve().parent
    try:
        changes,song_id,old_music,rate,map_file,mixer_changed=build_changes(root,pkg,args.quality,args.target,
                                                                     not args.no_assign,args.force_map_music)
    except (Refuse,FileNotFoundError,KeyError,ValueError) as exc:
        raise SystemExit('STOP (no files changed): '+str(exc))
    diffs={rel:new for rel,new in changes.items() if not (root/rel).is_file() or (root/rel).read_bytes()!=new}
    print('PokéKyoto:',root)
    print('Track:',MUSIC, '(ID',song_id,')')
    print('Map:',map_file, '| previous music:',old_music)
    print('PCM source:',rate,'Hz, signed 8-bit mono, full original orchestral recording')
    if args.quality=='hq':
        print('HQ mode: 26758 Hz global audio mixer'+(' [will be enabled]' if mixer_changed else ' [already enabled]'))
    if args.quality=='compatible':
        print('Compatible PCM only; does NOT downgrade an installed Littleroot HQ global mixer.')
    print('Changed files:')
    for f in diffs:print(' -',f)
    if not diffs:
        print('Already installed; no changes necessary.');return 0
    if args.dry_run:
        print('DRY RUN SUCCESS; no changes were made.');return 0
    backup=root/'.pokekyoto_route1_hq_backups'/datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    original=[];new_files=[]
    for rel in diffs:
        inp=root/rel
        if inp.is_file():
            out=backup/rel;out.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(inp,out);original.append(rel)
        else:new_files.append(rel)
    backup.mkdir(parents=True,exist_ok=True)
    (backup/'restore_manifest.json').write_text(json.dumps({
        'track':MUSIC,'song_id':song_id,'quality':args.quality,'previous_map_music':old_music,
        'originals_present':original,'files_new':new_files,'modified':list(diffs)
    },indent=2)+'\n')
    try:
        for rel,data in diffs.items():
            dst=root/rel;dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes(data)
    except BaseException as err:
        for rel in original:shutil.copy2(backup/rel,root/rel)
        for rel in new_files:(root/rel).unlink(missing_ok=True)
        raise SystemExit('Could not finish install; previous files restored: '+str(err))
    print('Installed. Backup:',backup)
    print('Build: make -j4')
    print('IMPORTANT: actual ROM audio, music loop, game FPS, and end-to-end playback still need a real mGBA test.')
    return 0

if __name__=='__main__':sys.exit(main())
