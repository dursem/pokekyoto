#!/usr/bin/env python3
"""Port approved PokéKyoto plateau/border V2 to source, without fixed ROM addresses.

Python 3.9+; standard library only. The default is a read-only check.
No build, git write, download, config rewrite, or whole-file source replacement.
"""
from __future__ import annotations
import argparse
import datetime as dt
import difflib
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import sys
import tempfile
import uuid

VERSION = 'pokekyoto-plateaus-v2-source-20261028'
MARKER = 'POKEKYOTO_PLATEAUS_V2_SOURCE'
HOME = Path(__file__).resolve().parent
CODE_PATHS = (
    'include/battle_bg.h', 'src/battle_bg.c', 'src/battle_anim_mons.c',
    'src/battle_controllers.c', 'src/battle_message.c',
)
ASSET_INPUT_GIT_SHA1 = {
    'graphics/battle_environment/kyoto_grass/tiles.png': '85e1a0367f3ea79144601f1f42db6e87ae51ef31',
    'graphics/battle_environment/kyoto_grass/map.bin': '5d6d586430c84812b2b2ef142b78e107f9af392d',
    'graphics/battle_environment/kyoto_land/tiles.png': '0b9ac7fa627c773ca369a97fc55b6ba53a86f00d',
    'graphics/battle_environment/kyoto_land/map.bin': '73dc51ae97eb065621478b10aac73d65341bcff2',
}
PALETTE_INPUT_GIT_SHA1 = {
    'graphics/battle_environment/kyoto_grass/palette.pal': '9b53f88cde026ac2d50e39e5e1cb2957c3cf5bc5',
    'graphics/battle_environment/kyoto_land/palette.pal': '9b8099be3655dacaae68cea955a09b2196e22e87',
}
ASSET_OUTPUT_SHA256 = {
    'graphics/battle_environment/kyoto_grass/tiles.png': '4075f6c359dec7af5f5ff06e0e10bbfaf16927338e74393af4e54f54c61317d4',
    'graphics/battle_environment/kyoto_grass/map.bin': '6618481eb889f10c6b417bebe6e2a2c4f665ca50d304ca3970b131e12f76b515',
    'graphics/battle_environment/kyoto_land/tiles.png': '2db45a8a06d767ed6322a48a3b8cfe8ecc7a6f09d49e586c9fd5018d86cabed2',
    'graphics/battle_environment/kyoto_land/map.bin': '1ba6de6e9a64e785c8d3de04758e4346f38ca7cc4f537085dc482227c320eb95',
}
ALL_PATHS = CODE_PATHS + tuple(ASSET_INPUT_GIT_SHA1)
ROM_BASE, ROM_CAPACITY = 0x08000000, 32 * 1024 * 1024

CENTER_HELPER = '''// POKEKYOTO_PLATEAUS_V2_SOURCE: same horizontal anchors as the approved V2 ROM.
// Use the displayed environment, not just gBattleEnvironment: maps/trainers
// can override it. Leave all Y offsets, contests and doubles layouts unchanged.
s16 KyotoBattlePlatformX(u32 battler, s16 defaultX)
{
    if (GetBattlerCoordsIndex(battler) != BATTLE_COORDS_SINGLES || IsContest())
        return defaultX;

    u32 position = GetBattlerPosition(battler);
    if (position != B_POSITION_PLAYER_LEFT
     && position != B_POSITION_OPPONENT_LEFT)
        return defaultX;

    switch (GetBattleEnvironmentOverride())
    {
    case BATTLE_ENVIRONMENT_GRASS:
        return position == B_POSITION_PLAYER_LEFT ? 60 : 178;
    case BATTLE_ENVIRONMENT_PLAIN:
        return position == B_POSITION_PLAYER_LEFT ? 64 : 174;
    default:
        return defaultX;
    }
}
'''
BORDER_OLD = '''        if (windowId == B_WIN_ACTION_NAME && gBattleScripting.windowsType == B_WIN_TYPE_NORMAL)
            FillWindowPixelBuffer(windowId, PIXEL_FILL(1));
        else'''
BORDER_NEW = '''        if (windowId == B_WIN_ACTION_NAME && gBattleScripting.windowsType == B_WIN_TYPE_NORMAL)
        {
            // POKEKYOTO_PLATEAUS_V2_SOURCE: preserve the frame beside the nickname.
            // The 11x2-tile window begins at x=8; its first pixel is border index 4.
            // Keep the existing text origin and the dynamic nickname + " do?".
            FillWindowPixelBuffer(windowId, PIXEL_FILL(1));
            u8 *pixels = (u8 *)GetWindowAttribute(windowId, WINDOW_TILE_DATA);
            u32 tileRowStride = GetWindowAttribute(windowId, WINDOW_WIDTH) * 32;
            for (u32 y = 0; y < 16; ++y)
            {
                u32 offset = (y / 8) * tileRowStride + (y % 8) * 4;
                pixels[offset] = (pixels[offset] & 0xF0) | 4;
            }
        }
        else'''

class PatchError(RuntimeError):
    pass

def require(ok, message):
    if not ok:
        raise PatchError(message)

def sha256(b):
    return hashlib.sha256(b).hexdigest()

def git_blob_sha1(b):
    return hashlib.sha1(b'blob ' + str(len(b)).encode('ascii') + b'\0' + b).hexdigest()

def read_regular(root, relative):
    """Refuse symlinked paths, including symlinked parent directories."""
    rel = Path(relative)
    require(not rel.is_absolute() and '..' not in rel.parts, 'Ongeldig pad: ' + str(relative))
    p = root
    for part in rel.parts:
        p = p / part
        require(not p.is_symlink(), 'Symlink niet toegestaan: ' + str(p))
    require(p.is_file(), 'Bestand ontbreekt: ' + str(p))
    return p.read_bytes()

def mask_c(text):
    """Retain positions/newlines while hiding comments and string literals."""
    rx = r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    return re.sub(rx, lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]), text, flags=re.S)

def function_span(text, name):
    masked = mask_c(text)
    rx = r'\b' + re.escape(name) + r'\s*\([^;{}]*\)\s*\{'
    matches = list(re.finditer(rx, masked))
    require(len(matches) == 1, f'Functie niet uniek gevonden: {name}. Niets aangepast.')
    m = matches[0]
    end, depth = m.end(), 1
    while end < len(masked) and depth:
        depth += (masked[end] == '{') - (masked[end] == '}')
        end += 1
    require(depth == 0, f'Onvolledige functie: {name}')
    return m.start(), m.end(), end

def replace_once(text, old, new, label):
    # Idempotent: only accept the exact generated replacement, not a marker alone.
    if new in text:
        require(text.count(new) == 1 and old not in text,
                f'Gemengde/dubbele patch bij {label}; handmatige controle nodig.')
        return text
    require(text.count(old) == 1, f'Onverwachte broncode bij {label}; niets overschreven.')
    return text.replace(old, new, 1)

def edit_function(text, name, edit):
    start, opening_end, end = function_span(text, name)
    old = text[start:end]
    new = edit(old, opening_end - start)
    return text[:start] + new + text[end:]

def include_after(text, header, anchor):
    new = '#include "' + header + '"'
    if re.search(r'(?m)^\s*#include\s+"' + re.escape(header) + r'"\s*$', text):
        return text
    old = '#include "' + anchor + '"'
    require(text.count(old) == 1, 'Include-anker ontbreekt: ' + anchor)
    return text.replace(old, old + '\n' + new, 1)

def validate_name_window(text):
    """The tiny border fix is only valid for the approved action-name window."""
    matches = list(re.finditer(r'\[B_WIN_ACTION_NAME\]\s*=\s*\{([^{}]*)\}', mask_c(text)))
    require(len(matches) == 1, 'Naamvenster niet uniek; patch gestopt.')
    values = dict(re.findall(r'\.(\w+)\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*,', matches[0][1]))
    expected = {'bg': 0, 'tilemapLeft': 1, 'tilemapTop': 37,
                'width': 11, 'height': 2, 'paletteNum': 12, 'baseBlock': 0x0170}
    require(all(k in values and int(values[k], 0) == v for k, v in expected.items()),
            'Het naamvenster is lokaal gewijzigd. De patch overschrijft dit niet.')

def transform_code(path, text):
    if path == 'include/battle_bg.h':
        old = 'void DrawMainBattleBackground(void);'
        new = old + '\n// ' + MARKER + '\ns16 KyotoBattlePlatformX(u32 battler, s16 defaultX);'
        # old is a prefix of new, so use a dedicated idempotency test.
        if new in text:
            require(text.count('s16 KyotoBattlePlatformX(') == 1, 'Dubbele helperdeclaratie.')
            return text
        require('KyotoBattlePlatformX' not in text, 'Afwijkende helperdeclaratie gevonden.')
        require(text.count(old) == 1, 'Header-anker ontbreekt.')
        return text.replace(old, new, 1)
    if path == 'src/battle_bg.c':
        validate_name_window(text)
        text = include_after(text, 'battle_interface.h', 'battle_bg.h')
        text = include_after(text, 'contest.h', 'battle_interface.h')
        if CENTER_HELPER in text:
            require(text.count('s16 KyotoBattlePlatformX(') == 1, 'Dubbele centerfunctie.')
            return text
        require('KyotoBattlePlatformX' not in text, 'Afwijkende centerfunctie gevonden.')
        _, _, end = function_span(text, 'GetBattleEnvironmentOverride')
        return text[:end] + '\n\n' + CENTER_HELPER.rstrip('\n') + text[end:]
    if path == 'src/battle_anim_mons.c':
        text = include_after(text, 'battle_bg.h', 'battle_anim.h')
        old = '        retVal = sBattlerCoords[GetBattlerCoordsIndex(battler)][GetBattlerPosition(battler)].x;'
        new = '''        // POKEKYOTO_PLATEAUS_V2_SOURCE: only the horizontal anchor changes.
        retVal = KyotoBattlePlatformX(battler,
            sBattlerCoords[GetBattlerCoordsIndex(battler)][GetBattlerPosition(battler)].x);'''
        return edit_function(text, 'GetBattlerSpriteCoord',
                             lambda body, _: replace_once(body, old, new, 'Pokemon X-positie'))
    if path == 'src/battle_controllers.c':
        text = include_after(text, 'battle_bg.h', 'battle_anim.h')
        addition = '''
    // POKEKYOTO_PLATEAUS_V2_SOURCE: match the trainer intro to its platform.
    xPos = KyotoBattlePlatformX(battler, xPos);
'''
        def intro(body, opening_end):
            if addition in body:
                require(body.count(addition) == 1, 'Dubbele trainer-intropatch.')
                return body
            require('KyotoBattlePlatformX' not in body, 'Trainer-intro heeft een andere patch.')
            return body[:opening_end] + addition + body[opening_end:]
        text = edit_function(text, 'BtlController_HandleDrawTrainerPic', intro)
        def slide(body, _):
            # Restrict both replacements to this function; don't shift other sprites.
            rx = r'(CreateSprite\(&gMultiuseSpriteTemplate,\s*)80(\s*,)'
            newx = 'KyotoBattlePlatformX(battler, 80)'
            if newx not in body:
                require(len(list(re.finditer(rx, body))) == 1, 'Player-slide X-anker ontbreekt.')
                body = re.sub(rx, lambda m: m[1] + newx + m[2], body, count=1)
            else:
                require(body.count(newx) == 1 and not re.search(rx, body), 'Dubbele player-slidepatch.')
            old = 'CreateSprite(&gMultiuseSpriteTemplate, 176, 40, 0)'
            new = 'CreateSprite(&gMultiuseSpriteTemplate, KyotoBattlePlatformX(battler, 208) - 32, 40, 0)'
            body = replace_once(body, old, new, 'opponent trainer-slide')
            require(body.count('.x += 32;') == 1,
                    'De opponent-slidecompensatie is gewijzigd; patch gestopt.')
            return body
        return edit_function(text, 'BtlController_HandleTrainerSlide', slide)
    if path == 'src/battle_message.c':
        return edit_function(text, 'BattlePutTextOnWindow',
                             lambda body, _: replace_once(body, BORDER_OLD, BORDER_NEW, 'naamkaderrand'))
    raise PatchError('Onverwacht codepad: ' + path)

def check_graphics_pipeline(root):
    graphics = read_regular(root, 'src/data/graphics/battle_environment.h').decode('utf-8')
    for env in ('grass', 'land'):
        for suffix, fmt in [('tiles.png', '.4bpp.smol'), ('map.bin', '.smolTM')]:
            needle = f'"graphics/battle_environment/kyoto_{env}/{suffix}", "{fmt}"'
            require(graphics.count(needle) == 1,
                    f'Onverwachte graphics-buildregel voor {env}/{suffix}; handmatige controle nodig.')
    for path, expected in PALETTE_INPUT_GIT_SHA1.items():
        require(git_blob_sha1(read_regular(root, path)) == expected,
                'Lokaal aangepast palette: ' + path + '. Het wordt niet overschreven.')

def plan(root, payload=None):
    payload = payload or HOME / 'payload'
    makefile = read_regular(root, 'Makefile').decode('utf-8')
    require(re.search(r'(?m)^GAME_VERSION\s*\?=\s*EMERALD\s*$', makefile) is not None,
            'Geen ondersteunde PokéKyoto Emerald-projectmap.')
    check_graphics_pipeline(root)
    before, after = {}, {}
    for path in CODE_PATHS:
        data = read_regular(root, path)
        text = data.decode('utf-8')
        newline = '\r\n' if '\r\n' in text else '\n'
        if newline == '\r\n':
            require('\n' not in text.replace('\r\n', ''), 'Gemengde regeleinden: ' + path)
            text = text.replace('\r\n', '\n')
        changed = transform_code(path, text)
        before[path] = data
        after[path] = changed.replace('\n', newline).encode('utf-8')
    for path, expected in ASSET_INPUT_GIT_SHA1.items():
        data = read_regular(root, path)
        replacement = read_regular(payload, path)
        require(sha256(replacement) == ASSET_OUTPUT_SHA256[path], 'Beschadigd patchbestand: ' + path)
        require(data == replacement or git_blob_sha1(data) == expected,
                'Deze achtergrond is lokaal anders: ' + path + '. Niets overschreven.')
        before[path], after[path] = data, replacement
    require(set(after) == set(ALL_PATHS), 'Interne controle van patchscope mislukt.')
    return before, after

def running_builds(root):
    """Read-only check. Never kill another task or modify its files."""
    proc = Path('/proc')
    found = []
    if not proc.is_dir():
        return found
    for entry in proc.iterdir():
        if not entry.name.isdigit() or int(entry.name) == os.getpid():
            continue
        try:
            cwd = (entry / 'cwd').resolve(strict=True)
            if cwd != root and root not in cwd.parents:
                continue
            argv = (entry / 'cmdline').read_bytes().split(b'\0')
            if not argv or not argv[0]:
                continue
            exe = Path(os.fsdecode(argv[0])).name
            names = {'make', 'gmake', 'cc1', 'scaninc', 'mapjson', 'compresSmol', 'compresSmolTilemap'}
            is_installer = any(b'kyoto_space_only.py' in a or b'kyoto_optimize.py' in a for a in argv)
            if exe in names or exe.startswith('arm-none-eabi-') or is_installer:
                found.append((entry.name, exe))
        except (OSError, RuntimeError):
            continue
    return found

def atomic_write(path, data, mode=None):
    path.parent.mkdir(parents=True, exist_ok=True)
    require(not path.is_symlink(), 'Symlink geweigerd: ' + str(path))
    oldmode = stat.S_IMODE(path.stat().st_mode) if path.exists() else 0o644
    fd, tmp = tempfile.mkstemp(prefix='.' + path.name + '.', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        os.chmod(tmp, oldmode if mode is None else mode)
        os.replace(tmp, path)
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)

def rom_used(root):
    path = root / 'pokeemerald.map'
    if not path.is_file() or path.is_symlink():
        return None
    matches = re.findall(r'^\s*(0x[0-9a-fA-F]+)\s+__rom_end\s*=',
                         path.read_text(encoding='utf-8', errors='replace'), flags=re.M)
    if len(matches) != 1:
        return None
    used = int(matches[0], 16) - ROM_BASE
    return used if 0 <= used <= ROM_CAPACITY else None

def apply(root, before, after):
    require(not running_builds(root), 'Er draait nog een build in dit project. Eerst laten stoppen.')
    changed = [p for p in ALL_PATHS if before[p] != after[p]]
    if not changed:
        print('V2-bronpatch staat al volledig in dit project. Niets aangepast.')
        return None
    # Validate the entire snapshot again before creating backups or touching sources.
    for path in ALL_PATHS:
        require(read_regular(root, path) == before[path], 'Bronbestand veranderde tijdens controle: ' + path)
    stamp = dt.datetime.now().strftime('%Y%m%d-%H%M%S')
    backup = root.parent / (root.name + '-plateaus-v2-backup-' + stamp + '-' + uuid.uuid4().hex[:6])
    backup.mkdir()
    metadata = {'version': VERSION, 'root': str(root), 'status': 'backed_up',
                'before_used_bytes': rom_used(root), 'files': {}, 'artifacts': {}}
    for path in changed:
        target = backup / 'original' / path
        mode = stat.S_IMODE((root / path).stat().st_mode)
        atomic_write(target, before[path], mode)
        metadata['files'][path] = {'before_sha256': sha256(before[path]),
                                    'after_sha256': sha256(after[path]), 'mode': mode}
    # A local backup is NOT shipped in the package or compiled into the ROM.
    for name in ('pokeemerald.gba', 'pokeemerald.elf', 'pokeemerald.map'):
        p = root / name
        if p.is_file() and not p.is_symlink():
            data = p.read_bytes()
            atomic_write(backup / 'build_before' / name, data)
            metadata['artifacts'][name] = sha256(data)
    diff = ''
    for path in changed:
        if path.endswith(('.c', '.h')):
            diff += ''.join(difflib.unified_diff(
                before[path].decode('utf-8').splitlines(True),
                after[path].decode('utf-8').splitlines(True),
                fromfile='a/' + path, tofile='b/' + path))
        else:
            diff += f'# Binary payload: {path}; SHA-256 {sha256(after[path])}\n'
    atomic_write(backup / 'source_changes.patch', diff.encode('utf-8'))
    manifest = backup / 'manifest.json'
    atomic_write(manifest, (json.dumps(metadata, indent=2) + '\n').encode())
    touched = []
    try:
        # Keep the previous content available even for a Ctrl+C during replacement.
        for path in changed:
            require(read_regular(root, path) == before[path], 'Bronbestand gewijzigd: ' + path)
            touched.append(path)
            atomic_write(root / path, after[path])
        for path in ALL_PATHS:
            require(read_regular(root, path) == after[path], 'Controle na schrijven mislukt: ' + path)
        metadata['status'] = 'source_applied_build_not_run'
        atomic_write(manifest, (json.dumps(metadata, indent=2) + '\n').encode())
    except BaseException:
        for path in reversed(touched):
            atomic_write(root / path, before[path], metadata['files'][path]['mode'])
        metadata['status'] = 'source_rolled_back'
        atomic_write(manifest, (json.dumps(metadata, indent=2) + '\n').encode())
        raise
    print('V2-BRONPATCH TOEGEPAST: gras, land, horizontale plaatsing en naamkaderrand.')
    print('GEEN FRLG. Muziek, OW-sprites en ruimtebesparingsinstellingen niet gewijzigd.')
    print('Back-up en echte brondiff:', backup)
    print('Er is geen build gestart. Gebruik nu de normale opdracht: make -j4')
    print('Herstellen (alleen deze bronpatch):')
    print(f'python3 "{Path(__file__).resolve()}" "{root}" --restore "{backup}"')
    return backup

def restore(root, backup):
    require(not running_builds(root), 'Er draait nog een build. Herstel niet tijdens het bouwen.')
    raw = read_regular(backup, 'manifest.json')
    m = json.loads(raw)
    require(m.get('version') == VERSION and m.get('root') == str(root), 'Back-up hoort niet bij dit project.')
    entries = m.get('files', {})
    require(bool(entries) and set(entries) <= set(ALL_PATHS), 'Onjuist back-upmanifest.')
    originals = {}
    for path, info in entries.items():
        data = read_regular(backup, 'original/' + path)
        require(sha256(data) == info.get('before_sha256'), 'Back-up beschadigd: ' + path)
        current = read_regular(root, path)
        require(sha256(current) in (info.get('before_sha256'), info.get('after_sha256')),
                'Nieuwe lokale wijziging gevonden: ' + path + '. Herstel overschrijft die niet.')
        originals[path] = data
    # All files validated before any restore write.
    for path, data in originals.items():
        atomic_write(root / path, data, entries[path]['mode'])
    m['status'] = 'source_restored'
    atomic_write(backup / 'manifest.json', (json.dumps(m, indent=2) + '\n').encode())
    print('Alleen de bestanden van deze V2-bronpatch zijn hersteld. Voer make -j4 uit.')
    print('De bewaarde oude ROM staat, indien aanwezig, in:', backup / 'build_before')
    print('De huidige .gba wordt bij bronherstel niet stilzwijgend vervangen.')

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('project', type=Path)
    g = ap.add_mutually_exclusive_group()
    g.add_argument('--apply', action='store_true', help='controleer, maak back-up en pas bronpatch toe')
    g.add_argument('--check', action='store_true', help='alleen controleren (standaard)')
    g.add_argument('--restore', type=Path, metavar='BACKUP', help='herstel alleen deze bronpatch')
    g.add_argument('--rom-report', action='store_true', help='lees __rom_end uit de huidige linker-map')
    args = ap.parse_args()
    root = args.project.expanduser().resolve()
    require(root.is_dir(), 'Projectmap bestaat niet.')
    print('Patch:', VERSION, flush=True)
    if args.restore:
        restore(root, args.restore.expanduser().resolve())
        return 0
    if args.rom_report:
        used = rom_used(root)
        require(used is not None, 'Geen geldige pokeemerald.map. Bouw eerst met make -j4.')
        print(f'Gebruikte ROM-ruimte: {used:,} bytes ({100*used/ROM_CAPACITY:.2f}%).')
        print(f'Vrije ROM-ruimte: {ROM_CAPACITY-used:,} bytes ({(ROM_CAPACITY-used)/1048576:.2f} MiB).')
        print('Dit is de linker-map op schijf; alleen actueel na een geslaagde build.')
        return 0
    before, after = plan(root)
    changed = [p for p in ALL_PATHS if before[p] != after[p]]
    print(f'Controle geslaagd. {len(changed)} bestanden te wijzigen; GEEN FRLG.')
    for path in changed:
        print('  ' + path)
    if args.apply:
        apply(root, before, after)
    else:
        print('ALLEEN CONTROLE: geen bronbestanden, ROM of Git gewijzigd.')
        print('Toepassen: herhaal deze opdracht met --apply')
    return 0

if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print('\nGestopt. Bij onderbreking tijdens toepassen worden de aangeraakte bronnen hersteld.', file=sys.stderr)
        raise SystemExit(130)
    except (PatchError, OSError, UnicodeError, json.JSONDecodeError) as exc:
        print('STOP: ' + str(exc), file=sys.stderr)
        raise SystemExit(1)
