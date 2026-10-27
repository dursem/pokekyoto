#!/usr/bin/env python3
"""Install/repair PokeKyoto Route 1 HQ music; Python 3 standard library.

    python3 PokeKyoto_Route1_FIX/install_route1.py . --dry-run
    python3 PokeKyoto_Route1_FIX/install_route1.py .
    python3 PokeKyoto_Route1_FIX/install_route1.py . --assign route101

Optional when you don't have the ZIP but do have the MP3 + this script:
    python3 install_route1.py . --mp3 '/mnt/c/.../Route 1(1).mp3'
"""
from __future__ import annotations
import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
import sys
from datetime import datetime
from pathlib import Path
from tempfile import TemporaryDirectory
import wave

NAME='mus_kyoto_route1'
CONST='MUS_KYOTO_ROUTE1'
RATE=26758
MAPS={
    'route1': ('data/maps/Route1_Frlg/map.json', 'MAP_ROUTE1'),
    'route101': ('data/maps/Route101/map.json', 'MAP_ROUTE101'),
}
VOICE='''@ PokeKyoto original Route 1 orchestra: full recording (single PCM voice).
@ Keep pitch at MIDI note 60; the WAV/DirectSound sample loops indefinitely.
voice_group kyoto_route1
    voice_directsound 60, 0, DirectSoundWaveData_kyoto_route1, 255, 255, 255, 0
'''
SYMBOL='DirectSoundWaveData_kyoto_route1::'
INCLUDE='.include "sound/voicegroups/kyoto_route1.inc"'
DS_ENTRY='''\n\t.align 2
DirectSoundWaveData_kyoto_route1::
\t.incbin "sound/direct_sound_samples/kyoto_route1.bin"
'''
CONFIG_LINE='mus_kyoto_route1.mid:        -E -R0 -G_kyoto_route1 -V115'

class Stop(Exception): pass

def read_wav_bytes(path):
    raw=path.read_bytes()
    with wave.open(str(path), 'rb') as w:
        channels, width, rate, count = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        if (channels, width, rate)!=(1,1,RATE):
            raise Stop(f'Wrong WAV format: {channels} channels / {width} bytes / {rate} Hz; expected 8-bit mono / {RATE} Hz')
        wav_data=w.readframes(count)
    if len(wav_data)!=count:raise Stop('WAV frame count mismatch')
    # Loop markers are crucial. Without this the generated GBA sample stops at its end.
    if b'smpl' not in raw[:256] or count< RATE:
        raise Stop('WAV loop metadata missing, or recording unexpectedly short')
    signed=bytes(b^0x80 for b in wav_data)
    wave_header=struct.pack('<4I',0x40000000,RATE*1024,0,count)
    bin_data=wave_header+signed+b'\0'*((-count)%4)
    if len(bin_data)!=16+count+(-count)%4:raise Stop('GBA sample size is inconsistent')
    return raw,bin_data,count

def make_loop_wav(mp3, output):
    if not mp3.is_file():raise Stop(f'MP3 not found: {mp3}')
    ffmpeg=shutil.which('ffmpeg')
    if not ffmpeg:raise Stop('ffmpeg is required when using --mp3. Ubuntu: sudo apt update && sudo apt install -y ffmpeg')
    cmd=[ffmpeg,'-nostdin','-hide_banner','-loglevel','error','-i',str(mp3),
         '-map','0:a:0','-ac','1','-ar',str(RATE),'-f','u8','-acodec','pcm_u8','pipe:1']
    try:
        res=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True)
    except subprocess.CalledProcessError as e:
        raise Stop('Could not decode MP3: '+e.stderr.decode(errors='replace')[:300])
    audio=res.stdout
    if len(audio)<RATE:raise Stop('MP3 seems empty or too short')
    fmt=struct.pack('<HHIIHH',1,1,RATE,RATE,1,8)
    smpl=struct.pack('<9I',0,0,0,60,0,0,0,1,0)+struct.pack('<6I',0,0,0,len(audio)-1,0,0)
    chunks=[]
    for kind,data in ((b'fmt ',fmt),(b'smpl',smpl),(b'data',audio)):
        chunks.append(kind+struct.pack('<I',len(data))+data+(b'\0' if len(data)%2 else b''))
    whole=b'RIFF'+struct.pack('<I',4+sum(len(c) for c in chunks))+b'WAVE'+b''.join(chunks)
    output.write_bytes(whole)

def collect_changes(root,package,custom_mp3,assignment,force):
    required=['src/m4a.c','sound/song_table.inc','sound/songs/midi/midi.cfg',
              'sound/voice_groups.inc','sound/direct_sound_data.inc','include/constants/songs.h',
              'data/maps/Route1_Frlg/map.json','data/maps/Route101/map.json']
    missing=[p for p in required if not (root/p).is_file()]
    if missing:raise Stop('Not a complete PokeKyoto source directory; missing: '+', '.join(missing))
    if custom_mp3:
        wav_in=package/'assets'/'_generated_route1.wav'
        wav_in.parent.mkdir(parents=True,exist_ok=True)
        make_loop_wav(custom_mp3,wav_in)
    else:
        wav_in=package/'assets'/'kyoto_route1.wav'
        if not wav_in.is_file():raise Stop('WAV missing from package; use --mp3 with your locally saved Route 1(1).mp3')
    wav_data,bin_data,samples=read_wav_bytes(wav_in)
    mid=package/'assets'/'mus_kyoto_route1.mid'
    if mid.is_file():
        mid_data=mid.read_bytes()
    else:
        # Controller contains a sustained note and a sequence loop after note-on.
        # The PCM wave sample provides the actual music, and loops on its own.
        import base64
        mid_data=base64.b64decode('TVRoZAAAAAYAAQACAeBNVHJrAAAATgD/AzJQb2tlS3lvdG8gUm91dGUgMSAtIG9yaWdpbmFsIG9yY2hlc3RyYSBzYW1wbGUgbG9vcAD/UQMHoSCDYP8BAVuHQP8BAV3OEP8vAE1UcmsAAABQAP8DN1BDTSBzb25nIGNvbnRyb2xsZXI7IGF1ZGlvIGlzIE5PVCBzeW50aGVzaXplZCBmcm9tIE1JREkAwAAAsAd/AJA8f4L3AIA8AAD/LwA=')
    if not mid_data.startswith(b'MThd'):raise Stop('Invalid MIDI controller')
    change={
        'sound/direct_sound_samples/kyoto_route1.wav':wav_data,
        'sound/direct_sound_samples/kyoto_route1.bin':bin_data,
        'sound/songs/midi/mus_kyoto_route1.mid':mid_data,
    }
    data={p:(root/p).read_text('utf-8') for p in required}
    # Append to END to preserve every existing song ID, and reuse the earlier Route 1 ID if installed.
    table=data['sound/song_table.inc']
    entries=re.findall(r'^\s*song\s+(\w+)\s*,\s*MUSIC_PLAYER_\w+\s*,\s*\d+',table,re.M)
    if len(entries)<610 or entries[0]!='mus_dummy':raise Stop('Unrecognized song table format')
    positions=[i for i,x in enumerate(entries) if x==NAME]
    if len(positions)>1:raise Stop('Duplicate Route 1 song entries; please review your song table')
    if positions:
        song_id=positions[0]
    else:
        song_id=len(entries)
        m=re.search(r'\n\s*\.align 2\s*\ndummy_song_header:',table)
        if not m:raise Stop('Cannot locate safe song table insertion point')
        table=table[:m.start()]+f'\n\t@ Custom Kyoto Route 1 music (original recorded orchestra)\n\tsong {NAME}, MUSIC_PLAYER_BGM, 0'+table[m.start():]
    change['sound/song_table.inc']=table.encode()
    constants=data['include/constants/songs.h']
    ids=re.findall(r'^#define\s+'+CONST+r'\s+(\d+)\b',constants,re.M)
    if len(ids)>1:raise Stop('Duplicate song constant')
    if ids and int(ids[0])!=song_id:raise Stop(f'Music ID mismatch: songs.h={ids[0]} song_table={song_id}')
    if not ids:
        before='#define MUS_ROUTE118'
        if constants.count(before)!=1:raise Stop('Could not find song ID insertion point in songs.h')
        constants=constants.replace(before,f'#define {CONST:<28} {song_id} // Custom Route 1 orchestral song\n'+before,1)
    change['include/constants/songs.h']=constants.encode()
    cfg=data['sound/songs/midi/midi.cfg']
    cfg_lines=re.findall(r'^mus_kyoto_route1\.mid:[^\r\n]*',cfg,re.M)
    if len(cfg_lines)>1:raise Stop('Duplicate MIDI config entry')
    if cfg_lines:
        if '-G_kyoto_route1' not in cfg_lines[0]:raise Stop('Custom Route 1 config conflict')
        # Existing user-selected volume stays as configured; do not unexpectedly change it.
    else:cfg=cfg.rstrip()+'\n'+CONFIG_LINE+'\n'
    change['sound/songs/midi/midi.cfg']=cfg.encode()
    voices=data['sound/voice_groups.inc']
    if INCLUDE not in voices:voices=voices.rstrip()+'\n\n@ Custom Route 1 orchestra\n'+INCLUDE+'\n'
    if voices.count(INCLUDE)!=1:raise Stop('Duplicate voice group include')
    change['sound/voice_groups.inc']=voices.encode()
    direct=data['sound/direct_sound_data.inc']
    if SYMBOL not in direct:direct=direct.rstrip()+DS_ENTRY
    if direct.count(SYMBOL)!=1 or '"sound/direct_sound_samples/kyoto_route1.bin"' not in direct:
        raise Stop('Route 1 sample symbol conflicts with another definition')
    change['sound/direct_sound_data.inc']=direct.encode()
    voicefile=root/'sound/voicegroups/kyoto_route1.inc'
    if voicefile.exists() and b'DirectSoundWaveData_kyoto_route1' not in voicefile.read_bytes():
        raise Stop('Existing custom Route 1 voice has been replaced by something else; no files changed')
    change['sound/voicegroups/kyoto_route1.inc']=VOICE.encode()

    # Keep HQ music fast enough. The PCM sample header itself is sample-rate aware.
    mixer=data['src/m4a.c']
    low='| SOUND_MODE_FREQ_13379';high='| SOUND_MODE_FREQ_26758'
    if high in mixer:pass
    elif low in mixer:
        if mixer.count(low)!=1:raise Stop('Unrecognized audio frequency settings')
        mixer=mixer.replace(low,high,1)
        # Existing custom title sample might use no-resample; make it pitch-aware instead.
        for rel,sym in [('sound/voicegroups/kyoto_title.inc','DirectSoundWaveData_kyoto_title')]:
            p=root/rel
            if p.is_file():
                c=p.read_text('utf-8')
                if sym not in c:raise Stop('Unrecognized previous title music voice')
                c,cnt=re.subn(r'\bvoice_directsound_no_resample\s+(?=60\s*,\s*0\s*,\s*'+sym+r'\s*,)',
                              'voice_directsound ',c)
                if cnt:change[rel]=c.encode()
        # Other custom no-resample voices need manual review before a global mixer upgrade.
        for p in sorted((root/'sound/voicegroups').rglob('*.inc')):
            rel=p.relative_to(root).as_posix()
            check=change.get(rel,p.read_bytes())
            if b'voice_directsound_no_resample' in check:
                raise Stop('HQ mixer change needs manual review of no-resample voice: '+rel)
    else:raise Stop('Nonstandard audio mixer frequency; no automatic changes')
    change['src/m4a.c']=mixer.encode()

    if assignment!='none':
        rel,expected_id=MAPS[assignment]
        m=data[rel]
        mapj=json.loads(m)
        if mapj.get('id')!=expected_id:raise Stop('Wrong map ID in '+rel)
        if mapj.get('music')!=CONST:
            if mapj.get('music') not in ('MUS_RG_ROUTE1','MUS_ROUTE101') and not force:
                raise Stop('Map has custom music '+str(mapj.get('music'))+'; use --force-map to change it intentionally')
            m,count=re.subn(r'("music"\s*:\s*")[A-Z0-9_]+(")',
                 lambda a:a.group(1)+CONST+a.group(2),m,count=1)
            if count!=1:raise Stop('Could not change music in '+rel)
        change[rel]=m.encode()
    return change,song_id,samples

def main():
    ap=argparse.ArgumentParser(description='Repair or add Route 1 HQ PCM music to PokeKyoto')
    ap.add_argument('repository',nargs='?',default='.')
    ap.add_argument('--dry-run',action='store_true',help='Check changes without writing anything')
    ap.add_argument('--mp3',type=Path,help='Optional local MP3; re-encode instead of using the packaged WAV (needs ffmpeg)')
    ap.add_argument('--assign',choices=['none','route1','route101'],default='none',help='Default: only register the song; select it in Porymap yourself')
    ap.add_argument('--force-map',action='store_true')
    args=ap.parse_args()
    root=Path(args.repository).expanduser().resolve()
    pack=Path(__file__).resolve().parent
    try:
        changes,id_,samples=collect_changes(root,pack,args.mp3,args.assign,args.force_map)
    except (Stop, OSError, ValueError, KeyError) as e:
        print('STOP: Nothing changed:',e,file=sys.stderr)
        return 1
    modified={p:b for p,b in changes.items() if not (root/p).is_file() or (root/p).read_bytes()!=b}
    print('PokéKyoto:',root)
    print('MUSIC ID:',CONST,'=',id_)
    print('Recording:',round(samples/RATE,2),'seconds, HQ',RATE,'Hz, 8-bit mono')
    print('Map assignment:',args.assign)
    print('Files to change:',len(modified))
    for rel in modified:print('  ',rel)
    if args.dry_run:
        print('DRY RUN OK. No project files were changed.')
        return 0
    if not modified:
        print('Already installed; no changes required.')
        return 0
    backup=root/'.route1_audio_fix_backups'/datetime.now().strftime('%Y%m%d_%H%M%S_%f')
    old=[];created=[]
    backup.mkdir(parents=True,exist_ok=True)
    try:
        for rel in modified:
            dest=root/rel
            if dest.is_file():
                (backup/rel).parent.mkdir(parents=True,exist_ok=True)
                shutil.copy2(dest,backup/rel)
                old.append(rel)
            else:created.append(rel)
        (backup/'manifest.json').write_text(json.dumps({'previous':old,'new':created,'song':CONST,'id':id_},indent=2))
        for rel,contents in modified.items():
            dest=root/rel
            dest.parent.mkdir(parents=True,exist_ok=True)
            dest.write_bytes(contents)
    except Exception as e:
        for rel in old:shutil.copy2(backup/rel,root/rel)
        for rel in created:(root/rel).unlink(missing_ok=True)
        print('INSTALL FAILED; backed-up files restored:',e,file=sys.stderr)
        return 1
    print('INSTALLED. Backup:',backup)
    print('Confirm: grep MUS_KYOTO_ROUTE1 include/constants/songs.h')
    print('Now: make -j4  (then close/reopen Porymap for the song list)')
    print('Actual ROM/mGBA playback still requires a test.')
    return 0

if __name__=='__main__':sys.exit(main())
