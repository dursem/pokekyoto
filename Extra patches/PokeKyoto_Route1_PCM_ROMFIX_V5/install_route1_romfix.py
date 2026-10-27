#!/usr/bin/env python3
"""Fix the 32-MiB ROM overflow after installing PokéKyoto Route 1 PCM V4.

Only replaces data/kyoto_audio/route1_pcm.bin. No song IDs, map assignments,
voicegroup volumes, music mixer settings or unrelated assets are changed.
Python 3 standard library only; no ffmpeg needed when using this ZIP.
"""
from __future__ import annotations

import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import sys
import tempfile

SAMPLE_REL = Path('data/kyoto_audio/route1_pcm.bin')
LABEL = 'DirectSoundWaveData_kyoto_route1'
CONST = 'MUS_KYOTO_ROUTE1'
PKG = Path(__file__).resolve().parent
BACKUP_DIR = '.pokekyoto_route1_romfix_v5_backups'
SAMPLE_SECONDS_MIN = 132.0
SAMPLE_SECONDS_MAX = 133.5

class SafeStop(Exception):
    pass

def check(condition: bool, message: str) -> None:
    if not condition:
        raise SafeStop(message)

def info(binary: bytes) -> dict:
    check(len(binary) >= 16, 'Sample te klein om geldig GBA-audio te zijn.')
    flags, freq, loop_start, count = struct.unpack('<4I', binary[:16])
    check(flags == 0x40000000, 'Verwachte PCM8 + LOOP; dit bestand is geen ondersteunde sample.')
    check(freq % 1024 == 0, 'GBA samplefrequentie is ongeldig.')
    rate = freq // 1024
    check(13000 <= rate <= 30000, f'Onverwachte samplefrequentie: {rate} Hz.')
    check(loop_start == 0, 'Sample heeft een andere loopstart; geen aanpassing uitgevoerd.')
    check(0 < count <= len(binary) - 16, 'Ongeldige samplelengte in GBA-header.')
    check((len(binary) - 16) - count < 4, 'Onjuiste lengte of PCM-bufferuitlijning.')
    seconds = count / rate
    check(SAMPLE_SECONDS_MIN <= seconds <= SAMPLE_SECONDS_MAX,
          f'Onverwachte nummerlengte: {seconds:.2f} sec.')
    return {'rate': rate, 'seconds': seconds, 'bytes': len(binary),
            'sha256': hashlib.sha256(binary).hexdigest()}

def verify_project(root: Path) -> Path:
    path = root / SAMPLE_REL
    check(path.is_file(), 'Route 1 PCM V4 niet gevonden: ' + str(path) +
          '\nInstalleer eerst V4; dit is alleen een ROM-grootte-reparatie.')
    src = root / 'sound/direct_sound_data.inc'
    check(src.is_file(), 'Geen PokéKyoto source-map: sound/direct_sound_data.inc ontbreekt.')
    data = src.read_text(encoding='utf-8')
    pat = (r'(?m)^\s*' + re.escape(LABEL) + r'::\s*\n\s*'
           r'\.incbin\s+"' + re.escape(SAMPLE_REL.as_posix()) + r'"')
    check(len(re.findall(pat, data)) == 1,
          'PCM-sample is niet gekoppeld zoals verwacht; stop uit voorzorg.')
    definitions = root / 'include/constants/songs.h'
    check(definitions.is_file() and
          re.search(r'(?m)^#define\s+' + CONST + r'\s+\d+\b', definitions.read_text(encoding='utf-8')),
          'MUS_KYOTO_ROUTE1 ontbreekt in constants/songs.h.')
    table = root / 'sound/song_table.inc'
    check(table.is_file() and 'song mus_kyoto_route1,' in table.read_text(encoding='utf-8'),
          'Route 1 is niet geregistreerd in sound/song_table.inc.')
    return path

def touch_sound(root: Path) -> None:
    # Explicitly force reassembly of the .incbin dependency on older build setups.
    src = root / 'data/sound_data.s'
    if src.is_file():
        os.utime(src, None)

def install(root: Path, quality: str, dry_run: bool) -> None:
    target = verify_project(root)
    old_bytes = target.read_bytes()
    old = info(old_bytes)
    rate = {'safe': 19500, 'maximum': 20000}[quality]
    replacement = PKG / 'assets' / f'route1_pcm{rate}.bin'
    check(replacement.is_file(), 'Audiobestand ontbreekt: ' + str(replacement))
    new_bytes = replacement.read_bytes()
    new = info(new_bytes)
    check(new['rate'] == rate, 'Sample heeft verkeerde samplefrequentie.')
    check(abs(old['seconds'] - new['seconds']) < 0.02,
          'Oude en nieuwe audio hebben niet dezelfde speelduur; stop.')
    print(f'Huidige Route 1: {old["rate"]:,} Hz, {old["bytes"]:,} bytes')
    print(f'Nieuwe Route 1:  {new["rate"]:,} Hz, {new["bytes"]:,} bytes')
    print(f'Besparing:       {old["bytes"] - new["bytes"]:+,} bytes')
    print('Muziekvolume, kaarttoewijzingen en globale mixer blijven ONGEWIJZIGD.')
    if old_bytes == new_bytes:
        print('AL GOED: deze PCM-versie staat al geïnstalleerd.')
        return
    if dry_run:
        print('DRY RUN OK: nog niets veranderd.')
        return
    backup = root / BACKUP_DIR / datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    backup.mkdir(parents=True, exist_ok=False)
    shutil.copy2(target, backup / 'route1_pcm.bin')
    (backup / 'manifest.json').write_text(json.dumps({
        'path': SAMPLE_REL.as_posix(), 'old': old, 'new': new,
        'warning': 'Restoring the older sample can make the ROM exceed 32 MiB again.'
    }, indent=2), encoding='utf-8')
    fd, tmp = tempfile.mkstemp(dir=target.parent, prefix='route1_pcm.romfix.tmp.')
    try:
        with os.fdopen(fd, 'wb') as f:
            f.write(new_bytes)
        os.replace(tmp, target)
        touch_sound(root)
        assert target.read_bytes() == new_bytes
    except Exception:
        shutil.copy2(backup / 'route1_pcm.bin', target)
        touch_sound(root)
        if os.path.exists(tmp):
            os.unlink(tmp)
        raise
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)
    print('GELUKT: alleen de PCM-sample is vervangen.')
    print('Back-up:', backup)
    print('Volgende stap: make -j4')

def restore(root: Path) -> None:
    target = verify_project(root)
    backups = root / BACKUP_DIR
    check(backups.is_dir(), 'Geen eerdere ROMFIX V5-back-up gevonden.')
    choices = sorted(p for p in backups.iterdir() if
                     (p / 'route1_pcm.bin').is_file())
    check(choices, 'Geen bruikbare back-up gevonden.')
    latest = choices[-1] / 'route1_pcm.bin'
    data = latest.read_bytes()
    info(data)
    shutil.copy2(latest, target)
    touch_sound(root)
    print('Vorige PCM-sample teruggezet uit', latest)
    print('LET OP: de oude sample kan opnieuw een ROM overflow veroorzaken.')

def main() -> int:
    parser = argparse.ArgumentParser(description='PokéKyoto Route 1 PCM V4: ROM-size fix (no DPCM).')
    parser.add_argument('project', nargs='?', default='.', help='PokéKyoto repository directory')
    parser.add_argument('--quality', choices=['safe','maximum'], default='safe',
                        help='safe=19500 Hz (~108 KB vrij), maximum=20000 Hz (~42 KB vrij)')
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--restore', action='store_true', help='Restore last Route 1 PCM backup')
    a = parser.parse_args()
    root = Path(a.project).expanduser().resolve()
    try:
        if a.restore:
            restore(root)
        elif a.check:
            p = verify_project(root)
            d = info(p.read_bytes())
            print('CHECK OK:', CONST, f'{d["rate"]} Hz, {d["seconds"]:.2f} sec, {d["bytes"]:,} bytes')
        else:
            install(root, a.quality, a.dry_run)
        return 0
    except (SafeStop, OSError, ValueError, UnicodeError, AssertionError) as exc:
        print('STOP:', exc, file=sys.stderr)
        return 1

if __name__ == '__main__':
    sys.exit(main())
