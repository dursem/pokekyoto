#!/usr/bin/env python3
"""PokéKyoto Route 1 - PCM V4 quality repair (Python 3, standard library).

Replaces the *audio format* (no DPCM) without changing existing volumes,
map assignments or the game's global sound mode.

From the PokéKyoto project folder:
  python3 PokeKyoto_Route1_PCM_V4/install_route1_pcm.py . --dry-run
  python3 PokeKyoto_Route1_PCM_V4/install_route1_pcm.py .
  python3 PokeKyoto_Route1_PCM_V4/install_route1_pcm.py . --check
  make -j4
"""
from __future__ import annotations
import argparse
import base64
from datetime import datetime
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile

NAME = 'mus_kyoto_route1'
CONST = 'MUS_KYOTO_ROUTE1'
SAMPLE_LABEL = 'DirectSoundWaveData_kyoto_route1'
SAMPLE_REL = 'data/kyoto_audio/route1_pcm.bin'
VOICE_REL = 'sound/voicegroups/kyoto_route1.inc'
VOICE_INCLUDE = '.include "sound/voicegroups/kyoto_route1.inc"'
MIDI_REL = 'sound/songs/midi/mus_kyoto_route1.mid'
# Built-in controller makes this script usable as a standalone download.
BUILTIN_MIDI_B64 = 'TVRoZAAAAAYAAQACAeBNVHJrAAAATgD/AzJQb2tlS3lvdG8gUm91dGUgMSAtIG9yaWdpbmFsIG9yY2hlc3RyYSBzYW1wbGUgbG9vcAD/UQMHoSCDYP8BAVuHQP8BAV3OEP8vAE1UcmsAAABQAP8DN1BDTSBzb25nIGNvbnRyb2xsZXI7IGF1ZGlvIGlzIE5PVCBzeW50aGVzaXplZCBmcm9tIE1JREkAwAAAsAd/AJA8f4L3AIA8AAD/LwA='
CONFIG_LINE = 'mus_kyoto_route1.mid:        -E -R0 -G_kyoto_route1 -V115'
TABLE_ENTRY = '\tsong mus_kyoto_route1, MUSIC_PLAYER_BGM, 0'

REQUIRED = ('sound/song_table.inc', 'sound/songs/midi/midi.cfg',
            'sound/voice_groups.inc', 'sound/direct_sound_data.inc',
            'include/constants/songs.h')
OLD_SAMPLE_PATHS = {
    'data/kyoto_audio/route1.dpcm',
    'sound/direct_sound_samples/kyoto_route1.bin',
    SAMPLE_REL,
}

class InstallError(Exception):
    pass

def require(value, message):
    if not value:
        raise InstallError(message)

def pcm_info(data, rate):
    require(len(data) >= 16 + rate * 30, 'Route 1 PCM-sample is too short')
    flags, freq, loop_start, loop_end = struct.unpack('<IIII', data[:16])
    require(flags == 0x40000000, 'Expected uncompressed looped PCM; no DPCM allowed')
    require(freq == rate * 1024, 'Incorrect GBA source pitch')
    require(loop_start == 0, 'Unexpected loop start')
    require(0 < loop_end <= len(data) - 16, 'PCM sample count is invalid')
    require(len(data) - 16 - loop_end < 4, 'Bad PCM buffer alignment')
    seconds = loop_end / rate
    require(130 <= seconds <= 135, 'The audio must be the entire Route 1 track')
    return seconds

def generate_from_mp3(mp3, rate):
    ffmpeg = shutil.which('ffmpeg')
    require(ffmpeg, 'ffmpeg is required for --mp3: sudo apt install ffmpeg')
    require(mp3.is_file(), f'MP3 not found: {mp3}')
    # Same filter chain as the packaged asset; FFmpeg quantizes directly to PCM8.
    cmd = [ffmpeg, '-nostdin', '-v', 'error', '-i', str(mp3), '-vn',
           '-af', f'pan=mono|c0=0.5*c0+0.5*c1,highpass=f=23,aresample={rate}:resampler=soxr:precision=28:cutoff=0.97',
           '-ac', '1', '-ar', str(rate), '-c:a', 'pcm_s8', '-f', 's8', '-']
    try:
        result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
    except (OSError, subprocess.CalledProcessError) as err:
        raise InstallError('MP3 conversion failed: ' + str(err)) from err
    pcm = result.stdout
    padding = (-len(pcm)) % 4
    return struct.pack('<4I', 0x40000000, rate*1024, 0, len(pcm)) + pcm + bytes(padding)

def audio_source(pkg, args):
    sr = 21024 if args.quality == 'balanced' else 18157
    if args.mp3:
        data = generate_from_mp3(args.mp3, sr)
    else:
        filename = f'route1_pcm{sr}.bin'
        p = pkg / 'assets' / filename
        require(p.is_file(), f'Asset not found: {p}. Use --mp3 for your own original recording.')
        data = p.read_bytes()
    seconds = pcm_info(data, sr)
    return data, sr, seconds

def bind_sample(text):
    pattern = re.compile(r'(?m)^([ \t]*' + re.escape(SAMPLE_LABEL) + r'::[ \t]*\r?\n)'
                         r'([ \t]*\.incbin[ \t]+"([^"]+)"[ \t]*$)')
    matches = list(pattern.finditer(text))
    require(len(matches) <= 1, 'Multiple Route 1 sample symbols found; stopped')
    if matches:
        m = matches[0]
        old = m.group(3)
        require(old in OLD_SAMPLE_PATHS, 'Unknown Route 1 sample path: ' + old)
        return text[:m.start()] + m.group(1) + '\t.incbin "' + SAMPLE_REL + '"' + text[m.end():]
    require(SAMPLE_LABEL + '::' not in text, 'Unknown Route 1 assembly layout; stopped')
    return text.rstrip('\n') + ('\n\n\t@ PokéKyoto Route 1 - uncompressed signed 8-bit PCM\n'
           '\t.align 2\n' + SAMPLE_LABEL + '::\n\t.incbin "' + SAMPLE_REL + '"\n')

def create_changes(root, pkg, args):
    missing = [p for p in REQUIRED if not (root / p).is_file()]
    require(not missing, 'This is not a PokéKyoto source folder: missing ' + ', '.join(missing))
    old = {p: (root/p).read_text(encoding='utf-8') for p in REQUIRED}
    pcm, sr, seconds = audio_source(pkg, args)
    changes = {SAMPLE_REL: pcm}

    song_table = old['sound/song_table.inc']
    songs = re.findall(r'^\s*song\s+(\w+)\s*,\s*MUSIC_PLAYER_\w+\s*,\s*\d+', song_table, re.M)
    require(len(songs) >= 610 and songs[0] == 'mus_dummy', 'Unrecognized song table')
    require(songs.count(NAME) <= 1, 'Duplicate Route 1 song entries')
    if NAME in songs:
        song_id = songs.index(NAME)
    else:
        song_id = len(songs)
        m = re.search(r'\n\s*\.align 2\s*\ndummy_song_header:', song_table)
        require(m, 'Song-table insertion point not found')
        song_table = song_table[:m.start()] + '\n\t@ Kyoto Route 1\n' + TABLE_ENTRY + song_table[m.start():]
    changes['sound/song_table.inc'] = song_table.encode()

    const = old['include/constants/songs.h']
    definitions = re.findall(r'^#define\s+' + CONST + r'\s+(\d+)\b', const, re.M)
    require(len(definitions) <= 1, 'Duplicate Route 1 constants')
    if definitions:
        require(int(definitions[0]) == song_id, 'Route 1 music ID does not match its song-table position')
    else:
        require(const.count('#define MUS_ROUTE118') == 1, 'Could not safely insert song constant')
        const = const.replace('#define MUS_ROUTE118', f'#define {CONST:<29} {song_id} // PokeKyoto PCM Route 1\n#define MUS_ROUTE118', 1)
    changes['include/constants/songs.h'] = const.encode()

    cfg = old['sound/songs/midi/midi.cfg']
    lines = re.findall(r'(?m)^mus_kyoto_route1\.mid:[^\r\n]*', cfg)
    require(len(lines) <= 1, 'Duplicate Route 1 midi.cfg entries')
    if lines:
        require('-G_kyoto_route1' in lines[0], 'Route 1 MIDI uses unexpected voicegroup')
        # Preserve existing -V volume and all other MIDI settings, as requested.
    else:
        cfg = cfg.rstrip('\n') + '\n' + CONFIG_LINE + '\n'
    changes['sound/songs/midi/midi.cfg'] = cfg.encode()

    groups = old['sound/voice_groups.inc']
    require(groups.count(VOICE_INCLUDE) <= 1, 'Duplicate Route 1 voice group include')
    if VOICE_INCLUDE not in groups:
        groups = groups.rstrip('\n') + '\n\n@ PokeKyoto Route 1\n' + VOICE_INCLUDE + '\n'
    changes['sound/voice_groups.inc'] = groups.encode()

    current_voice = root/VOICE_REL
    if current_voice.is_file():
        v = current_voice.read_text('utf-8')
        require(v.count(SAMPLE_LABEL) == 1, 'Existing Route 1 voice is not recognized')
        # Keep the voice parameters; no change to the song's volume.
    else:
        v = ('@ Route 1 original orchestra; uncompressed sample\n'
             'voice_group kyoto_route1\n'
             '    voice_directsound 60, 0, DirectSoundWaveData_kyoto_route1, 255, 255, 255, 0\n')
    changes[VOICE_REL] = v.encode()

    ds = bind_sample(old['sound/direct_sound_data.inc'])
    require(ds.count(SAMPLE_LABEL + '::') == 1, 'Duplicate Route 1 sample definition')
    changes['sound/direct_sound_data.inc'] = ds.encode()

    midi_path = root / MIDI_REL
    if not midi_path.exists():
        midi = pkg/'assets'/'mus_kyoto_route1.mid'
        data = midi.read_bytes() if midi.is_file() else base64.b64decode(BUILTIN_MIDI_B64)
        require(data.startswith(b'MThd'), 'MIDI controller is invalid')
        changes[MIDI_REL] = data

    # Do not change map assignments or any files related to the global sound mixer.
    return changes, song_id, sr, seconds

def install(root, changes, dry_run):
    changed = {rel: data for rel,data in changes.items()
               if not (root/rel).is_file() or (root/rel).read_bytes() != data}
    print('Files to update:', len(changed))
    for name in changed:
        print(' ', name)
    if dry_run:
        print('DRY RUN OK; no project files changed.')
        return
    if not changed:
        print('Already installed. Rebuild with make -j4 if needed.')
        return
    backup = root/'.pokekyoto_route1_pcm_v4_backups'/datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    backup.mkdir(parents=True, exist_ok=False)
    originals=[]; fresh=[]
    try:
        for rel in changed:
            dest=root/rel
            if dest.is_file():
                orig=backup/rel
                orig.parent.mkdir(parents=True,exist_ok=True)
                shutil.copy2(dest,orig)
                originals.append(rel)
            else:
                fresh.append(rel)
        (backup/'manifest.json').write_text(json.dumps({'restore':originals,'delete':fresh}, indent=2),encoding='utf-8')
        for rel,data in changed.items():
            dest=root/rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            fd, tmp = tempfile.mkstemp(dir=dest.parent, prefix=dest.name+'.tmp.')
            try:
                with os.fdopen(fd,'wb') as file:
                    file.write(data)
                os.replace(tmp, dest)
            finally:
                if os.path.exists(tmp):
                    os.unlink(tmp)
    except Exception:
        for rel in originals:
            shutil.copy2(backup/rel, root/rel)
        for rel in fresh:
            (root/rel).unlink(missing_ok=True)
        raise
    print('SUCCESS: sample is uncompressed PCM; source changes are saved.')
    print('Backup:',backup)

def check(root):
    constants=(root/'include/constants/songs.h').read_text('utf-8')
    data=(root/SAMPLE_REL).read_bytes()
    freq=struct.unpack('<I', data[4:8])[0]//1024
    seconds=pcm_info(data,freq)
    voice=(root/'sound/direct_sound_data.inc').read_text('utf-8')
    require(CONST in constants, 'Route 1 music constant is missing')
    require(SAMPLE_REL in voice, 'PCM sample not linked to sound data')
    require(NAME in (root/'sound/song_table.inc').read_text('utf-8'), 'Route 1 song table entry missing')
    print('CHECK OK:', CONST, '- uncompressed PCM',freq,'Hz,',round(seconds,2),'seconds')
    print('No changes to existing map assignments, master volume, or global mixer.')

def main():
    p=argparse.ArgumentParser(description='Repair PokéKyoto Route 1 audio using uncompressed signed 8-bit PCM')
    p.add_argument('project', nargs='?', default='.')
    p.add_argument('--quality',choices=['balanced','compact'],default='balanced',help='balanced: 21024 Hz, compact: 18157 Hz')
    p.add_argument('--mp3',type=Path,help='Optional original MP3 on your own computer when packaged audio is unavailable')
    p.add_argument('--dry-run',action='store_true')
    p.add_argument('--check',action='store_true')
    a=p.parse_args()
    root=Path(a.project).expanduser().resolve()
    try:
        if a.check:
            check(root)
            return 0
        changes, song_id, sr, seconds=create_changes(root,Path(__file__).resolve().parent,a)
        print('Song:',CONST,'=',song_id)
        print('Format: UNCOMPRESSED signed PCM8,',sr,'Hz,',round(seconds,2),'sec, loop enabled')
        print('Sample size in ROM: %.2f MiB' % (len(changes[SAMPLE_REL])/(1024**2)))
        print('Existing volumes/mixer/map assignments: UNCHANGED')
        print('If ROM exceeds 32 MiB, retry with --quality compact')
        install(root, changes, a.dry_run)
        if not a.dry_run:
            print('Next: python3 PokeKyoto_Route1_PCM_V4/install_route1_pcm.py . --check')
            print('Next: make -j4')
        return 0
    except (InstallError, OSError, ValueError, UnicodeError) as e:
        print('STOP:',e,file=sys.stderr)
        return 1

if __name__ == '__main__':
    raise SystemExit(main())
