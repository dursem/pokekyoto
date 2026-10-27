#!/usr/bin/env python3
"""PokéKyoto: make existing Route 1 PCM V4/V5 music louder safely.

No downloads or new instruments: edits only the existing signed-8-bit PCM
and the Route 1 entry in midi.cfg. Other songs, mix rate, and ROM size unchanged.
Uses stdlib Python 3 only. A dated backup and a state file prevent double gain.
"""
from __future__ import annotations
import argparse
from array import array
from datetime import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import sys
import tempfile

AUDIO = Path('data/kyoto_audio/route1_pcm.bin')
CFG = Path('sound/songs/midi/midi.cfg')
MARKER = Path('.pokekyoto_route1_volume_v6_state.json')
BACKUPS = Path('.pokekyoto_route1_volume_v6_backups')
MIDI_NAME = 'mus_kyoto_route1.mid'
DEFAULT_GAIN_DB = 3.0
CEILING = 0.94

class Stop(Exception):
    pass

def need(c, message):
    if not c:
        raise Stop(message)

def digest(data):
    return hashlib.sha256(data).hexdigest()

def metrics(buf):
    a = array('b')
    a.frombytes(buf)
    squares = sum(x * x for x in a)
    rms = math.sqrt(squares / len(a)) / 128.0 if a else 0.0
    peak = max((abs(x) for x in a), default=0) / 128.0
    return round(20 * math.log10(max(rms, 1e-12)), 2), round(20 * math.log10(max(peak, 1e-12)), 2)

def check_source(root):
    for p in (AUDIO, CFG, Path('include/constants/songs.h'), Path('sound/song_table.inc'), Path('sound/direct_sound_data.inc')):
        need((root / p).is_file(), f'Bestand ontbreekt: {p}. Eerst Route 1 PCM V4/V5 installeren.')
    need(re.search(r'(?m)^#define\s+MUS_KYOTO_ROUTE1\s+\d+\b', (root/'include/constants/songs.h').read_text('utf-8')), 'MUS_KYOTO_ROUTE1 ontbreekt.')
    need('song mus_kyoto_route1,' in (root/'sound/song_table.inc').read_text('utf-8'), 'Route 1 staat niet in de songtabel.')
    ss = (root/'sound/direct_sound_data.inc').read_text('utf-8')
    need(re.search(r'DirectSoundWaveData_kyoto_route1::\s*\n\s*\.incbin\s+"data/kyoto_audio/route1_pcm.bin"', ss), 'Route 1 is niet gekoppeld aan het verwachte PCM-bestand.')
    wav = (root/AUDIO).read_bytes()
    need(len(wav) > 16, 'Onjuist audioformaat.')
    flags, rate1024, loop_start, count = struct.unpack('<4I', wav[:16])
    rate = rate1024 // 1024
    need(flags == 0x40000000 and rate1024 == rate*1024 and 13000 <= rate <= 30000, 'Niet-ondersteund sampleformaat (verwacht PCM8 met loop).')
    need(loop_start == 0 and 0 < count <= len(wav) - 16 and (len(wav) - 16 - count) < 4, 'Ongeldige samplelengte of loop.')
    seconds = count / rate
    need(130 <= seconds <= 135, f'Onverwachte speelduur {seconds:.1f} s; gestopt voor de veiligheid.')
    cfg_bytes = (root/CFG).read_bytes()
    cfg = cfg_bytes.decode('utf-8')
    lines = re.findall(r'(?m)^mus_kyoto_route1\.mid:[^\r\n]*', cfg)
    need(len(lines) == 1, 'Route 1 ontbreekt in midi.cfg of is dubbel ingevoerd.')
    oldline = lines[0]
    need('-G_kyoto_route1' in oldline, 'Route 1 gebruikt een onverwachte voicegroup.')
    volumes = re.findall(r'(?<!\S)-V(\d+)\b', oldline)
    need(len(volumes) == 1 and int(volumes[0]) <= 127, 'Geen geldige -V volumewaarde in midi.cfg.')
    return wav, count, rate, seconds, cfg_bytes, cfg, oldline, int(volumes[0])

def limiter_boost(pcm, rate, gain_db):
    """PCM8 gain + slow-releasing short lookahead peak limiter.

    The limiter is usually inactive: it only reduces gain around unusually
    strong transients. This avoids hard clipping/distorting strings and bells.
    """
    data = array('b')
    data.frombytes(pcm)
    gain = 10 ** (gain_db / 20.0)
    # 256 samples ~= 13.1 ms at 19.5kHz.
    block = 256
    nblocks = (len(data) + block - 1) // block
    maxima = [max((abs(v) for v in data[i*block:(i+1)*block]), default=0) for i in range(nblocks)]
    ceiling_int = int(round(CEILING * 127))
    desired = [min(1.0, ceiling_int / (v * gain)) if v else 1.0 for v in maxima]
    # 2 blocks look-ahead/behind; smooth release over ~80ms.
    windowed = [min(desired[max(0,i-2):min(nblocks,i+3)]) for i in range(nblocks)]
    gains = [1.0] * nblocks
    recovery = block / (rate * 0.085)
    for i, target in enumerate(windowed):
        gains[i] = min(target, (gains[i-1] + recovery if i else target), 1.0)
    out = bytearray(len(data))
    adjusted = 0
    for bi in range(nblocks):
        a = bi * block
        b = min(len(data), a+block)
        g0 = gains[bi]
        g1 = gains[bi+1] if bi+1 < nblocks else g0
        delta = (g1-g0) / (b-a)
        g = g0
        for i in range(a,b):
            v = int(round(data[i] * gain * g))
            # Last-resort limiter should never be needed at this ceiling,
            # but guarantees no digital clipping even for unusual material.
            if v > ceiling_int: v = ceiling_int
            if v < -ceiling_int: v = -ceiling_int
            out[i] = v & 0xff
            g += delta
        if g0 < 0.999 or g1 < 0.999:
            adjusted += 1
    return bytes(out), adjusted / nblocks * 100

def create_file_atomically(p, content):
    fd, tmp = tempfile.mkstemp(dir=p.parent, prefix=p.name + '.voltmp.')
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(content)
        os.replace(tmp, p)
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)

def touch_sound(root):
    path = root / 'data/sound_data.s'
    if path.is_file():
        os.utime(path, None)

def restore(root):
    marker = root / MARKER
    need(marker.is_file(), 'Geen V6-installatie gevonden die teruggezet kan worden.')
    state = json.loads(marker.read_text('utf-8'))
    need((root/AUDIO).is_file() and digest((root/AUDIO).read_bytes()) == state['after_sha256'], 'Het audiobestand is sindsdien gewijzigd; veilig terugzetten niet mogelijk.')
    need((root/CFG).is_file() and digest((root/CFG).read_bytes()) == state['after_config_sha256'], 'De MIDI-configuratie is sindsdien gewijzigd; veilig terugzetten niet mogelijk.')
    backup = root / state['backup']
    need((backup/'route1_pcm.bin').is_file() and (backup/'midi.cfg').is_file(), 'Back-up ontbreekt.')
    create_file_atomically(root/AUDIO, (backup/'route1_pcm.bin').read_bytes())
    create_file_atomically(root/CFG, (backup/'midi.cfg').read_bytes())
    marker.unlink()
    touch_sound(root)
    print('TERUGGEZET: origineel Route 1-volume en midi.cfg. Bouw opnieuw: make -j4')

def apply(root, gain_db, dry_run=False):
    wav, count, rate, seconds, cfgbytes, cfg, line, vol = check_source(root)
    marker = root/MARKER
    if marker.exists():
        state = json.loads(marker.read_text('utf-8'))
        if (digest(wav) == state.get('after_sha256') and digest(cfgbytes) == state.get('after_config_sha256')):
            print(f'AL GEREED: Route 1 V6-volume is al ingesteld (+{state["gain_db"]:.1f} dB PCM, -V{state["new_midi_volume"]}). Geen tweede versterking.')
            return
        raise Stop('V6 was eerder geïnstalleerd, maar de bestanden zijn daarna aangepast. Om dubbele versterking te voorkomen gestopt.')
    need(-0.01 <= gain_db <= 6, 'gain-db moet tussen 0 en 6 liggen.')
    header, pcm, padding = wav[:16], wav[16:16+count], wav[16+count:]
    newpcm, limitpercent = limiter_boost(pcm, rate, gain_db)
    newwav = header + newpcm + padding
    newcfg = cfg.replace(line, re.sub(r'(?<!\S)-V\d+\b', '-V127', line, count=1), 1).encode('utf-8')
    rms_before, peak_before = metrics(pcm)
    rms_after, peak_after = metrics(newpcm)
    print(f'Route 1: {seconds:.2f} seconden, {rate} Hz, PCM8, {len(wav):,} bytes')
    print(f'PCM-versterking: +{gain_db:.1f} dB; MIDI-volume: {vol}/127 -> 127/127')
    print(f'Gemeten PCM RMS: {rms_before:+.2f} -> {rms_after:+.2f} dBFS')
    print(f'Piekniveau: {peak_before:+.2f} -> {peak_after:+.2f} dBFS, piekbegrenzing in {limitpercent:.2f}% van de blokken')
    print('Bestandsgrootte/tempo/samplefrequentie/loop blijven exact hetzelfde.')
    print('Littleroot, titelscherm, Route 2 en globale mixer blijven ongewijzigd.')
    if dry_run:
        print('DRY RUN: niets gewijzigd.')
        return
    backup = root / BACKUPS / datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    backup.mkdir(parents=True, exist_ok=False)
    shutil.copy2(root/AUDIO, backup/'route1_pcm.bin')
    shutil.copy2(root/CFG, backup/'midi.cfg')
    try:
        create_file_atomically(root/AUDIO, newwav)
        create_file_atomically(root/CFG, newcfg)
        state = {'gain_db': gain_db, 'before_sha256': digest(wav), 'after_sha256': digest(newwav),
                 'before_config_sha256': digest(cfgbytes), 'after_config_sha256': digest(newcfg),
                 'previous_midi_volume': vol, 'new_midi_volume': 127,
                 'backup': backup.relative_to(root).as_posix(), 'bytes': len(wav)}
        create_file_atomically(marker, json.dumps(state, indent=2).encode('utf-8'))
        touch_sound(root)
    except Exception:
        create_file_atomically(root/AUDIO, wav)
        create_file_atomically(root/CFG, cfgbytes)
        if marker.exists(): marker.unlink()
        touch_sound(root)
        raise
    print('GELUKT: alleen Route 1 is harder gezet. Back-up:', backup)
    print('Volgende opdracht: make -j4')

def main():
    p = argparse.ArgumentParser(description='Route 1 PCM8 volume omhoog, zonder kwaliteitsverlies of grotere ROM')
    p.add_argument('project', nargs='?', default='.', help='Pad naar je PokéKyoto-project')
    p.add_argument('--gain-db', type=float, default=DEFAULT_GAIN_DB, help='PCM-gain in decibel; standaard 3.0')
    p.add_argument('--dry-run', action='store_true')
    p.add_argument('--restore', action='store_true')
    args = p.parse_args()
    root = Path(args.project).expanduser().resolve()
    try:
        if args.restore: restore(root)
        else: apply(root, args.gain_db, args.dry_run)
        return 0
    except (Stop, OSError, ValueError, UnicodeError, KeyError) as e:
        print('STOP:', e, file=sys.stderr)
        return 1

if __name__ == '__main__':
    sys.exit(main())
