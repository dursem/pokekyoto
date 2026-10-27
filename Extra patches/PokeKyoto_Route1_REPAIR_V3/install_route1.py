#!/usr/bin/env python3
"""PokeKyoto Route 1 repair v3; Python 3 only, no extra dependencies.

Default installs REGISTERED MUSIC ONLY, without touching the global sound mixer.

    python3 PokeKyoto_Route1_REPAIR_V3/install_route1.py . --dry-run
    python3 PokeKyoto_Route1_REPAIR_V3/install_route1.py .
    python3 PokeKyoto_Route1_REPAIR_V3/install_route1.py . --check

Optional HQ mode (changes GBA mixer settings; READ README before using!):
    python3 PokeKyoto_Route1_REPAIR_V3/install_route1.py . --enable-hq
"""
from __future__ import annotations

import argparse
from datetime import datetime
import hashlib
import json
import os
import subprocess
from pathlib import Path
import re
import shutil
import struct
import sys
import tempfile
import wave

NAME = 'mus_kyoto_route1'
CONST = 'MUS_KYOTO_ROUTE1'
SAMPLE_NAME = 'DirectSoundWaveData_kyoto_route1'
SAMPLE_REL = 'data/kyoto_audio/route1.dpcm'
RATE = 26758
FLAGS = 0x40000001  # DPCM and loop
CONFIG = 'mus_kyoto_route1.mid:        -E -R0 -G_kyoto_route1 -V115'
VOICE_REL = 'sound/voicegroups/kyoto_route1.inc'
VOICE_INCLUDE = '.include "sound/voicegroups/kyoto_route1.inc"'
TABLE_ENTRY = '\tsong mus_kyoto_route1, MUSIC_PLAYER_BGM, 0'
VOICE = '''@ PokeKyoto Route 1 - streamed-style full-recording sample (DPCM in ROM).
@ 26,758 Hz source. MIDI note 60 is held while the sample loops internally.
voice_group kyoto_route1
    voice_directsound 60, 0, DirectSoundWaveData_kyoto_route1, 255, 255, 255, 0
'''
REQUIRED = (
    'sound/song_table.inc', 'sound/songs/midi/midi.cfg',
    'sound/voice_groups.inc', 'sound/direct_sound_data.inc',
    'include/constants/songs.h', 'src/m4a.c',
)
MAPPING = {
    'route1': ('data/maps/Route1_Frlg/map.json', 'MAP_ROUTE1'),
    'route101': ('data/maps/Route101/map.json', 'MAP_ROUTE101'),
}

class InstallerError(Exception):
    pass

def ensure(condition, message):
    if not condition:
        raise InstallerError(message)

def read(root, rel):
    return (root / rel).read_text(encoding='utf-8')

def replace_directsound_binding(text):
    # Preserve the existing symbol and alignment; change only this exact incbin.
    pat = (r'(?m)^(?P<label>[ \t]*'+re.escape(SAMPLE_NAME)+r'::[ \t]*\r?\n)'
           r'(?P<line>[ \t]*\.incbin[ \t]+"[^"]*"[ \t]*$)')
    matches = list(re.finditer(pat, text))
    if len(matches) > 1:
        raise InstallerError('Duplicate Route 1 sample symbol; no changes made')
    if len(matches) == 1:
        m = matches[0]
        old_path = re.search(r'\.incbin[ \t]+"([^"]+)"', m.group('line')).group(1)
        if old_path not in (SAMPLE_REL, 'sound/direct_sound_samples/kyoto_route1.bin'):
            raise InstallerError('Unexpected existing Route 1 sample path: '+old_path)
        return text[:m.start()] + m.group('label') + '\t.incbin "'+SAMPLE_REL+'"' + text[m.end():]
    return (text.rstrip('\n') +
        '\n\n\t@ PokeKyoto Route 1 compact DPCM recording\n'
        '\t.align 2\n'+SAMPLE_NAME+'::\n'
        '\t.incbin "'+SAMPLE_REL+'"\n')

def load_audio_payload(assets, project, mp3):
    if mp3 is None:
        p=assets/'kyoto_route1.dpcm'
        ensure(p.is_file(),
               'Package sample missing. Supply --mp3 with the original Windows MP3 path.')
        return p.read_bytes()
    ensure(mp3.is_file(), 'MP3 file was not found: '+str(mp3))
    ffmpeg=shutil.which('ffmpeg')
    ensure(ffmpeg is not None, 'Install ffmpeg in Ubuntu: sudo apt install ffmpeg')
    tool=project/'tools/wav2agb/wav2agb'
    if not tool.is_file():
        print('Building wav2agb compression tool...')
        try:
            subprocess.run(['make','-C',str(project/'tools/wav2agb')],
                           stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True)
        except (OSError,subprocess.CalledProcessError) as exc:
            raise InstallerError('Could not build wav2agb. Run make tools in your project.') from exc
    ensure(tool.is_file(),'Could not locate PokéKyoto wav2agb tool')
    with tempfile.TemporaryDirectory(prefix='kyoto_route1_') as directory:
        temporary=Path(directory)
        wav=temporary/'route1.wav'
        packed=temporary/'route1.dpcm'
        try:
            subprocess.run([ffmpeg,'-nostdin','-hide_banner','-loglevel','error',
                            '-y','-i',str(mp3),'-map','0:a:0','-ar',str(RATE),
                            '-ac','1','-acodec','pcm_u8',str(wav)],check=True)
            subprocess.run([str(tool),'-b','-c','-l','2','--loop-start','0',
                            str(wav),str(packed)],check=True)
        except (OSError,subprocess.CalledProcessError) as exc:
            raise InstallerError('MP3 conversion/compression failed: '+str(exc)) from exc
        return packed.read_bytes()

def make_changes(root, assets, args):
    missing = [rel for rel in REQUIRED if not (root/rel).is_file()]
    ensure(not missing, 'This is not a PokéKyoto source checkout. Missing: '+', '.join(missing))
    texts={rel:read(root,rel) for rel in REQUIRED}
    payload=load_audio_payload(assets,root,args.mp3)
    ensure(len(payload)>100000, 'Route 1 audio asset too short')
    flags,freq,start,count=struct.unpack('<4I', payload[:16])
    ensure((flags,freq,start)==(FLAGS,RATE*1024,0), 'Incorrect DPCM header or pitch')
    ensure(3_400_000<count<3_700_000, 'Unexpected original PCM sample count')
    expected_blocks=(count+63)//64
    ensure(len(payload)==16+expected_blocks*33, 'DPCM payload length is invalid')
    ensure((assets/'mus_kyoto_route1.mid').read_bytes().startswith(b'MThd'), 'Missing valid MIDI controller')
    changes={
        SAMPLE_REL:payload,
        'sound/songs/midi/mus_kyoto_route1.mid':(assets/'mus_kyoto_route1.mid').read_bytes(),
        VOICE_REL:VOICE.encode('utf-8'),
    }

    table=texts['sound/song_table.inc']
    entries=re.findall(r'^\s*song\s+(\w+)\s*,\s*MUSIC_PLAYER_\w+\s*,\s*\d+',table,re.M)
    ensure(len(entries)>=610 and entries[0]=='mus_dummy', 'Unexpected song table format')
    positions=[i for i,name in enumerate(entries) if name==NAME]
    ensure(len(positions)<=1, 'Duplicate mus_kyoto_route1 entries in song table')
    if positions:
        song_id=positions[0]
    else:
        song_id=len(entries)
        match=re.search(r'\n\s*\.align 2\s*\ndummy_song_header:',table)
        ensure(match is not None, 'Cannot find safe insertion point in song table')
        table=table[:match.start()]+'\n\t@ Kyoto Route 1 soundtrack\n'+TABLE_ENTRY+table[match.start():]
    changes['sound/song_table.inc']=table.encode()

    constants=texts['include/constants/songs.h']
    match=re.findall(r'^#define\s+'+CONST+r'\s+(\d+)\b',constants,re.M)
    ensure(len(match)<=1, 'Duplicate Route 1 song constants')
    if match:
        ensure(int(match[0])==song_id,
               'Existing music ID does not match song table: '+match[0]+' vs '+str(song_id))
    else:
        # No renumbering! Adding a new index past the PH_NURSE_SOLO entry.
        anchor='#define MUS_ROUTE118'
        ensure(constants.count(anchor)==1, 'Unable to insert the music constant safely')
        constants=constants.replace(anchor, f'#define {CONST:<29} {song_id} // PokéKyoto Route 1\n'+anchor, 1)
    changes['include/constants/songs.h']=constants.encode()

    cfg=texts['sound/songs/midi/midi.cfg']
    lines=re.findall(r'^mus_kyoto_route1\.mid:[^\r\n]*',cfg,re.M)
    ensure(len(lines)<=1,'Duplicate midi.cfg Route 1 entries')
    if lines:
        ensure('-G_kyoto_route1' in lines[0], 'Existing Route 1 music config has another voicegroup')
    else:
        cfg=cfg.rstrip('\n')+'\n'+CONFIG+'\n'
    changes['sound/songs/midi/midi.cfg']=cfg.encode()

    vg=texts['sound/voice_groups.inc']
    if VOICE_INCLUDE not in vg:
        vg=vg.rstrip('\n')+'\n\n@ PokéKyoto Route 1 orchestra\n'+VOICE_INCLUDE+'\n'
    ensure(vg.count(VOICE_INCLUDE)==1,'Conflicting or duplicated voice group entry')
    changes['sound/voice_groups.inc']=vg.encode()
    old_voice=root/VOICE_REL
    if old_voice.is_file():
        existing=old_voice.read_text(encoding='utf-8')
        ensure(SAMPLE_NAME in existing, 'Existing custom Route 1 voice is not recognized')

    ds=replace_directsound_binding(texts['sound/direct_sound_data.inc'])
    ensure(ds.count(SAMPLE_NAME+'::')==1, 'Route 1 sample symbol duplicated')
    changes['sound/direct_sound_data.inc']=ds.encode()

    if args.assign!='none':
        rel,expected_id=MAPPING[args.assign]
        ensure((root/rel).is_file(), 'Cannot find map to assign: '+rel)
        map_text=read(root,rel)
        parsed=json.loads(map_text)
        ensure(parsed.get('id')==expected_id,'Map mismatch at '+rel)
        old_song=parsed.get('music')
        if old_song!=CONST:
            if not args.force_map and old_song not in ('MUS_RG_ROUTE1','MUS_ROUTE101'):
                raise InstallerError('Map already uses custom music: '+str(old_song)+'; use --force-map')
            map_text,count_subs=re.subn(r'("music"\s*:\s*")[A-Z0-9_]+(")',
                lambda m:m.group(1)+CONST+m.group(2),map_text,count=1)
            ensure(count_subs==1,'Cannot find music value in map JSON')
        changes[rel]=map_text.encode()

    # DO NOT upgrade globally by default: there are numerous fixed-frequency
    # drum/sample voices in Pokemon Emerald. The previous patch failed here.
    # Explicit HQ opt-in only; back up every affected file first.
    sound=texts['src/m4a.c']
    if args.enable_hq:
        low='| SOUND_MODE_FREQ_13379'
        high='| SOUND_MODE_FREQ_26758'
        if high in sound:
            ensure(sound.count(high)==1,'Unexpected multiple HQ mixer declarations')
        elif low in sound:
            ensure(sound.count(low)==1, 'Cannot determine audio mixer initialization')
            sound=sound.replace(low,high,1)
        else:
            raise InstallerError('Unknown GBA mixer configuration; use normal registration without --enable-hq')
        changes['src/m4a.c']=sound.encode()
        patched_voices=0
        fixed_voices=0
        for voice in (root/'sound/voicegroups').rglob('*.inc'):
            rel=voice.relative_to(root).as_posix()
            body=(changes[rel].decode('utf-8') if rel in changes else voice.read_text('utf-8'))
            # Switch no-resample (mode 8) to pitch-aware (mode 0), preventing
            # obvious speed doubling of pre-existing percussion on 26k mixer.
            rx=r'(?m)^(\s*)voice_directsound_no_resample(?=\s)'
            new,num=re.subn(rx,r'\1voice_directsound',body)
            if num:
                patched_voices+=1
                fixed_voices+=num
                changes[rel]=new.encode()
        # Record these numbers for the user, not necessary to patch other config.
    else:
        patched_voices=fixed_voices=0
    return changes, song_id, count, patched_voices, fixed_voices

def check_installed(root):
    p=root/'include/constants/songs.h'
    if not p.is_file():
        raise InstallerError('Invalid project root: '+str(root))
    ok=CONST in p.read_text('utf-8')
    bin_ok=(root/SAMPLE_REL).exists()
    voice_ok=(root/'sound/voicegroups/kyoto_route1.inc').exists()
    print('Music ID:', 'PRESENT' if ok else 'MISSING')
    print('Audio sample:', 'PRESENT' if bin_ok else 'MISSING')
    print('Voicegroup:', 'PRESENT' if voice_ok else 'MISSING')
    if ok and bin_ok and voice_ok:
        print('Registered in the PokéKyoto source. Rebuild with make -j4 and restart Porymap.')
    else:
        raise InstallerError('Route 1 is not fully installed')

def main():
    parser=argparse.ArgumentParser(description='Register compact Route 1 audio safely in PokéKyoto')
    parser.add_argument('project', nargs='?', default='.', help='PokéKyoto root folder')
    parser.add_argument('--dry-run',action='store_true',help='Show changes without writing anything')
    parser.add_argument('--check',action='store_true',help='Check music registration')
    parser.add_argument('--enable-hq',action='store_true',help='Enable 26k global mixer and convert fixed-rate instruments (review/test in mGBA)')
    parser.add_argument('--assign',choices=['none','route1','route101'],default='none',help='Default: add to Porymap list without changing any map music')
    parser.add_argument('--force-map',action='store_true')
    parser.add_argument('--mp3',type=Path,default=None,help='Use an MP3 on your Windows/WSL drive when ZIP download is unavailable; requires ffmpeg and wav2agb')
    args=parser.parse_args()
    root=Path(args.project).expanduser().resolve()
    try:
        if args.check:
            check_installed(root)
            return 0
        changes, song_id, samples,voice_files,voices=make_changes(root,Path(__file__).resolve().parent/'assets',args)
        changed={rel:b for rel,b in changes.items() if not (root/rel).is_file() or (root/rel).read_bytes()!=b}
        print('Route 1 song ID:', CONST, '=', song_id)
        print('Audio:', round(samples/RATE,2),'seconds, 26758 Hz, looped DPCM,',
              round(len(changes[SAMPLE_REL])/1024/1024,2),'MiB in ROM')
        print('ROM capacity: maximum 32 MiB; verify the build fits.')
        print('Map assignment:',args.assign)
        if args.enable_hq:
            print('HQ global mixer requested. Fixed-rate instruments to change:', voices,
                  'across',voice_files,'voicegroup files.')
            print('WARNING: global HQ can affect other sounds and framerate; test in mGBA!')
        else:
            print('Safe registration: existing GBA sound mixer and percussion are not modified.')
            print('NOTE: full 26k fidelity requires optional --enable-hq after this install.')
        print('Files to change:',len(changed))
        for rel in changed:
            print(' ',rel)
        if args.dry_run:
            print('DRY RUN OK: no source files were modified.')
            return 0
        if not changed:
            print('Already installed; no changes required.')
            return 0
        backup=root/'.pokekyoto_route1_backups'/datetime.now().strftime('%Y%m%d-%H%M%S-%f')
        originals=[];fresh=[]
        backup.mkdir(parents=True,exist_ok=False)
        try:
            for rel in changed:
                dest=root/rel
                if dest.is_file():
                    saved=backup/rel
                    saved.parent.mkdir(parents=True,exist_ok=True)
                    shutil.copy2(dest,saved)
                    originals.append(rel)
                else:
                    fresh.append(rel)
            (backup/'restore_manifest.json').write_text(json.dumps({'restore':originals,'delete':fresh},indent=2))
            for rel,contents in changed.items():
                dest=root/rel
                dest.parent.mkdir(parents=True,exist_ok=True)
                # Write temp on same FS, then atomic rename to avoid partially-written content.
                fd,temp=tempfile.mkstemp(prefix=dest.name+'.tmp.',dir=dest.parent)
                try:
                    with os.fdopen(fd,'wb') as wf:
                        wf.write(contents)
                    os.replace(temp,dest)
                finally:
                    if os.path.exists(temp):os.unlink(temp)
        except Exception:
            for rel in originals:
                shutil.copy2(backup/rel,root/rel)
            for rel in fresh:
                (root/rel).unlink(missing_ok=True)
            raise
        print('\nSUCCESS: Route 1 is registered in the source!')
        print('Backup folder:',backup)
        print('Next: grep -n MUS_KYOTO_ROUTE1 include/constants/songs.h')
        print('Next: make -j4  and completely restart Porymap.')
        return 0
    except (InstallerError,OSError,ValueError,KeyError,UnicodeError,json.JSONDecodeError) as err:
        print('STOP: Nothing changed:',err,file=sys.stderr)
        return 1

if __name__=='__main__':
    raise SystemExit(main())
