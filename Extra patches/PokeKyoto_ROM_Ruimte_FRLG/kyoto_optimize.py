#!/usr/bin/env python3
"""Selective PokéKyoto source changes. No network, no ID renumbering.

Default: dry run and write a reviewable patch. --apply writes backed-up changes.
--build-compare builds BEFORE and AFTER, measures __rom_end, and rolls back on
build failure. Requires the project's working GNU ARM toolchain and dependencies.
This package has not been compiled as a complete PokéKyoto ROM by its author.
"""
from __future__ import annotations
import argparse
import collections
import dataclasses
import datetime as dt
import difflib
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

VERSION = 'kyoto-space-frlg-20261028-v2'
# Keep the original marker so an installed V1 cannot be applied a second time.
MARKER = 'KYOTO_SPACE_FRLG_V1'
BASE_COMMIT = 'add41daf166fdd251b1bb14e7b1dc22c3ed3b61b'
ROM_BASE, ROM_CAPACITY = 0x08000000, 32 * 1024 * 1024
FORMS = ('P_MEGA_EVOLUTIONS', 'P_GEN_9_MEGA_EVOLUTIONS',
         'P_ULTRA_BURST_FORMS', 'P_GIGANTAMAX_FORMS', 'P_TERA_FORMS')
UB_FAMILIES = ('NIHILEGO', 'BUZZWOLE', 'PHEROMOSA', 'XURKITREE',
               'CELESTEELA', 'KARTANA', 'GUZZLORD', 'POIPOLE',
               'STAKATAKA', 'BLACEPHALON')
GIMMICKS = ('MEGA', 'ULTRA_BURST', 'Z_MOVE', 'DYNAMAX', 'TERA')
CAN_FUNCTIONS = ('CanMegaEvolve', 'CanUltraBurst', 'CanUseZMove',
                 'CanDynamax', 'CanTerastallize')
ACTIVATE_FUNCTIONS = ('ActivateMegaEvolution', 'ActivateUltraBurst',
                      'ActivateZMove', 'ActivateDynamax', 'ActivateTera')
OBJECT_DIR = 'src/data/object_events/'
# Follower tables are a separate source context and reuse static names from the
# legacy NPC tables (for example sPicTable_Pidgey). They must NOT be merged into
# the NPC dependency index. This exclusion does not edit or remove the headers.
FOLLOWER_HEADERS = frozenset({
    'object_event_pic_tables_followers.h',
    'object_event_graphics_info_followers.h',
})

class PatchError(RuntimeError):
    pass

def require(ok: bool, message: str) -> None:
    if not ok:
        raise PatchError(message)

def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def mask_c(text: str) -> str:
    """Mask comments and literals, preserving all character positions."""
    pattern = r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    return re.sub(pattern, lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]),
                  text, flags=re.S)

def context_at(text: str, position: int) -> tuple[str, ...]:
    stack: list[str] = []
    for m in re.finditer(r'(?m)^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b([^\n]*)', mask_c(text)):
        if m.start() >= position:
            break
        op, cond = m[1], m[2].strip()
        if op in ('if', 'ifdef', 'ifndef'):
            stack.append(cond if op == 'if' else op + ' ' + cond)
        elif op == 'endif':
            require(bool(stack), 'Unbalanced preprocessor #endif')
            stack.pop()
        else:
            require(bool(stack), 'Unbalanced preprocessor branch')
            stack[-1] = op + ' ' + cond
    return tuple(stack)

def frlg_only(text: str, pos: int) -> bool:
    context = context_at(text, pos)
    if 'IS_FRLG' not in context:
        return False
    # Do not silently discard other build conditions when copying declarations.
    others = [x for x in context if x != 'IS_FRLG' and not x.startswith('ifndef GUARD_')]
    require(not others, 'Nested conditions around an FRLG declaration need review: ' + repr(others))
    return True

@dataclasses.dataclass
class Declaration:
    name: str
    start: int
    end: int
    equal: int
    body_start: int
    body_end: int
    text: str
    path: str


def declarations(text: str, path: str = '') -> dict[str, Declaration]:
    masked = mask_c(text)
    pattern = re.compile(r'(?m)^[ \t]*(?:static[ \t]+)?const[^\n;={}]*?\b'
                         r'([A-Za-z_]\w*)[ \t]*(?:\[[^\]\n]*\][ \t]*)*=[ \t]*')
    found: dict[str, Declaration] = {}
    for m in pattern.finditer(masked):
        pos = m.end()
        depth = 0
        first_brace = last_brace = -1
        while pos < len(masked):
            c = masked[pos]
            if c == '{':
                if first_brace < 0:
                    first_brace = pos
                depth += 1
            elif c == '}':
                depth -= 1
                if depth == 0:
                    last_brace = pos
            elif c == ';' and depth == 0:
                break
            pos += 1
        require(pos < len(masked), f'Unterminated declaration {path}:{m[1]}')
        name = m[1]
        require(name not in found, f'Multiple definitions of {path}:{name}; manual review required')
        found[name] = Declaration(name, m.start(), pos + 1, m.end() - 1,
                                  first_brace, last_brace, text[m.start():pos + 1], path)
    return found


def replace_define(text: str, name: str, value: str) -> str:
    pat = re.compile(r'(?m)^([ \t]*#define[ \t]+' + re.escape(name) + r'[ \t]+)([^\s/]+)')
    require(len(list(pat.finditer(text))) == 1, f'Expected one definition of {name}')
    return pat.sub(lambda m: m[1] + value, text, count=1)


def source_plan(root: Path) -> tuple[dict[str, bytes], dict[str, bytes], dict]:
    originals: dict[str, bytes] = {}
    texts: dict[str, str] = {}
    def read(path: str) -> str:
        if path not in texts:
            f = root / path
            require(f.is_file() and not f.is_symlink(), f'Missing or symlinked source: {path}')
            raw = f.read_bytes()
            originals[path] = raw
            text = raw.decode('utf-8')
            require(MARKER not in text, f'Patch marker already present: {path}. Use the existing report/rollback.')
            texts[path] = text
        return texts[path]

    require((root / 'Makefile').is_file(), 'This is not a decomp source directory.')
    require((root / 'sound/direct_sound_samples/kyoto_littleroot.wav').is_file(),
            'Kyoto Littleroot source missing: refusing to modify a different project.')
    makefile = (root / 'Makefile').read_text(encoding='utf-8')
    require('GAME_VERSION ?= EMERALD' in makefile, 'Expected the PokéKyoto Emerald build.')
    # Refuse known wrong project; never change PokePeach.
    remote = subprocess.run(['git', '-C', str(root), 'remote', 'get-url', 'origin'],
                            text=True, capture_output=True, check=False)
    if remote.returncode == 0:
        require('pokekyoto' in remote.stdout.lower(), 'origin is not PokéKyoto: ' + remote.stdout.strip())

    path = 'include/config/species_enabled.h'
    text = read(path)
    for name in FORMS + tuple('P_FAMILY_' + x for x in UB_FAMILIES):
        text = replace_define(text, name, 'FALSE')
    texts[path] = text
    path = 'include/config/pokemon.h'
    texts[path] = replace_define(read(path), 'P_MODIFIED_MEGA_CRIES', 'FALSE')

    path = 'src/data/gimmicks.h'
    text = read(path)
    masked = mask_c(text)
    edits = []
    for name in GIMMICKS:
        pat = re.compile(r'\[GIMMICK_' + name + r'\]\s*=\s*\{')
        matches = list(pat.finditer(masked))
        require(len(matches) == 1, f'Expected one GIMMICK_{name} table entry')
        m = matches[0]
        depth, end = 1, m.end()
        while depth and end < len(masked):
            depth += (masked[end] == '{') - (masked[end] == '}')
            end += 1
        require(depth == 0, f'Unbalanced GIMMICK_{name}')
        require(re.search(r'\b[A-Za-z_]*[Dd]omain[A-Za-z_]*\b', text[m.start():end]) is None,
                f'GIMMICK_{name} appears to be repurposed for Domain Expansion; review manually')
        edits.append((m.start(), end, f'[GIMMICK_{name}] = {{0}} /* {MARKER}: disabled */'))
    for start, end, new in sorted(edits, reverse=True):
        text = text[:start] + new + text[end:]
    texts[path] = text

    # Guard real entry points too, not just the player-selection menu.
    wanted = {x: 'FALSE' for x in CAN_FUNCTIONS}
    wanted.update({x: '' for x in ACTIVATE_FUNCTIONS})
    locations = collections.defaultdict(list)
    source_files = list((root / 'src').glob('*.c'))
    for f in source_files:
        raw = f.read_text(encoding='utf-8')
        if not any(re.search(r'\b' + name + r'\s*\(', raw) for name in wanted):
            continue
        masked = mask_c(raw)
        for name in wanted:
            pattern = (r'(?m)^[ \t]*(?:static\s+)?(?:inline\s+)?'
                       r'(bool32|bool8|void)\s+' + name + r'\s*\([^;{}]*\)\s*\{')
            for m in re.finditer(pattern, masked):
                depth, end = 1, m.end()
                while depth and end < len(masked):
                    depth += (masked[end] == '{') - (masked[end] == '}')
                    end += 1
                require(depth == 0, f'Unbalanced function {name}')
                require(re.search(r'\b[A-Za-z_]*[Dd]omain[A-Za-z_]*\b', raw[m.start():end]) is None,
                        f'{name} contains Domain Expansion references; review manually')
                locations[name].append((f.relative_to(root).as_posix(), m.end(), m[1]))
    grouped = collections.defaultdict(list)
    for name, result in wanted.items():
        require(len(locations[name]) == 1, f'Cannot uniquely locate function body {name}')
        path, offset, kind = locations[name][0]
        require((kind == 'void') == (result == ''), f'Unexpected return type for {name}: {kind}')
        grouped[path].append((offset, name, result))
    for path, items in grouped.items():
        text = read(path)
        for offset, name, result in sorted(items, reverse=True):
            guard = f'\n    /* {MARKER}: this built-in gimmick is not used in Kyoto. */\n    return{(" " + result) if result else ""};\n'
            text = text[:offset] + guard + text[offset:]
        texts[path] = text

    # Read the original object definitions. No global change to IS_FRLG.
    object_paths = [p.relative_to(root).as_posix()
                    for p in sorted((root / OBJECT_DIR).glob('*.h'))
                    if p.name not in FOLLOWER_HEADERS]
    defs: dict[str, Declaration] = {}
    for path in object_paths:
        for name, decl in declarations(read(path), path).items():
            require(name not in defs, 'Ambiguous NPC object dependency: ' + name +
                    ' (' + (defs[name].path if name in defs else '') + ' / ' + path + ')')
            defs[name] = decl
    pointer_path = OBJECT_DIR + 'object_event_graphics_info_pointers.h'
    pointers = read(pointer_path)
    tables = declarations(pointers, pointer_path)
    table = tables.get('gObjectEventGraphicsInfoPointers')
    require(table is not None and table.body_start >= 0, 'Cannot locate the OW graphics pointer table')
    pointer_regex = re.compile(r'(?m)^[ \t]*\[(OBJ_EVENT_GFX_\w+)\][ \t]*=[ \t]*&(gObjectEventGraphicsInfo_\w+)[ \t]*,')
    rows = list(pointer_regex.finditer(pointers, table.body_start, table.body_end))
    def dependency_graph(start: str) -> set[str]:
        seen, pending = set(), [start]
        while pending:
            name = pending.pop()
            if name in seen or name not in defs:
                continue
            seen.add(name)
            pending.extend(set(re.findall(r'\b[gs][A-Z]\w*\b', mask_c(defs[name].text))) - seen)
        return seen

    selected_rows = []
    needed: set[str] = set()
    palette_tags: set[str] = set()
    for m in rows:
        gfx_id, info = m[1], m[2]
        if not frlg_only(pointers, m.start()):
            continue
        require(info in defs, 'Missing OW definition ' + info)
        if re.match(r'OBJ_EVENT_GFX_(RED|GREEN)_', gfx_id):
            continue  # Player states are deliberately not enabled/changed.
        graph = dependency_graph(info)
        png_paths = [p for symbol in graph for p in re.findall(r'"(graphics/object_events/pics/[^\"]+)"', defs[symbol].text)]
        if not any('/pics/people/' in p for p in png_paths):
            continue  # No FRLG monsters, boats, rocks or decorative objects.
        require(not any('/people/red/' in p or '/people/leaf/' in p for p in png_paths),
                'Player graphics unexpectedly referenced by ' + gfx_id)
        selected_rows.append((gfx_id, info, m[0]))
        needed.update(graph)
        palette_tags.update(re.findall(r'\bOBJ_EVENT_PAL_TAG_\w+\b', defs[info].text))
    require(len(selected_rows) >= 20, 'Too few FRLG NPCs found; source layout needs review')

    movement_path = 'src/event_object_movement.c'
    movement = read(movement_path)
    palette_rows = list(re.finditer(r'(?m)^[ \t]*\{[ \t]*(gObjectEventPal_\w+)[ \t]*,[ \t]*(OBJ_EVENT_PAL_TAG_\w+)[ \t]*\}[ \t]*,', movement))
    tag_rows = {m[2]: m for m in palette_rows}
    new_palette_rows = []
    for tag in sorted(palette_tags):
        if tag == 'OBJ_EVENT_PAL_TAG_NONE':
            continue
        require(tag in tag_rows, 'Palette registration missing: ' + tag)
        for candidate in (tag, tag + '_REFLECTION'):
            if candidate not in tag_rows:
                continue
            m = tag_rows[candidate]
            if not frlg_only(movement, m.start()):
                continue
            require(m[1] in defs, 'Palette definition missing: ' + m[1])
            needed.update(dependency_graph(m[1]))
            new_palette_rows.append(m[0])

    # Copy only required declarations into !IS_FRLG sections in their original
    # headers, in the original declaration order. Original FRLG builds are unchanged.
    lifted = collections.defaultdict(list)
    for name in sorted(needed):
        d = defs[name]
        if frlg_only(read(d.path), d.start):
            lifted[d.path].append(d)
    for path, items in lifted.items():
        items.sort(key=lambda x: x.start)
        texts[path] = read(path).rstrip() + '\n\n#if !IS_FRLG\n/* ' + MARKER + ': FRLG NPC data in Emerald. */\n' + '\n\n'.join(d.text for d in items) + '\n#endif\n'

    # Add registrations in existing reserved ID slots; never insert enum IDs.
    externs = '\n#if !IS_FRLG\n/* ' + MARKER + ' */\n' + ''.join(
        'extern const struct ObjectEventGraphicsInfo ' + info + ';\n' for _, info, _ in selected_rows) + '#endif\n'
    added_rows = '\n#if !IS_FRLG\n/* ' + MARKER + ' */\n' + '\n'.join(x[2] for x in selected_rows) + '\n#endif\n'
    pointers = pointers[:table.body_end] + added_rows + pointers[table.body_end:]
    pointers = pointers[:table.start] + externs + pointers[table.start:]
    texts[pointer_path] = pointers

    require(bool(new_palette_rows), 'No FRLG NPC palettes found')
    # Insert before the original FRLG palette block, not after the NULL sentinel.
    first_palette = min(tag_rows[tag].start() for tag in palette_tags if tag in tag_rows and frlg_only(movement, tag_rows[tag].start()))
    guards = [m for m in re.finditer(r'(?m)^[ \t]*#if[ \t]+IS_FRLG[^\n]*\n', movement) if m.start() < first_palette]
    require(bool(guards), 'FRLG palette block not found')
    insertion = guards[-1].start()
    add = '#if !IS_FRLG\n/* ' + MARKER + ': register NPC palettes before the end marker. */\n' + '\n'.join(dict.fromkeys(new_palette_rows)) + '\n#endif\n'
    texts[movement_path] = movement[:insertion] + add + movement[insertion:]

    changed = {p: t.encode('utf-8') for p, t in texts.items() if t.encode('utf-8') != originals[p]}
    # A strict allowlist prevents edits to music, maps, graphics files, constants,
    # player/glide code, species names/data, moves, abilities or custom gimmicks.
    permitted = {'include/config/species_enabled.h', 'include/config/pokemon.h',
                 'src/data/gimmicks.h', movement_path, pointer_path} | set(grouped) | set(lifted)
    require(set(changed) <= permitted, 'Unexpected modified path')
    report = {'version': VERSION, 'reference_commit': BASE_COMMIT,
              'forms_disabled': list(FORMS), 'ultra_beast_families_disabled': list(UB_FAMILIES),
              'gimmicks_disabled': list(GIMMICKS),
              'frlg_npc_count': len(selected_rows), 'frlg_npc_ids': [x[0] for x in selected_rows],
              'lifted_declarations': sum(map(len, lifted.values())),
              'excluded_npc_index_headers': sorted(FOLLOWER_HEADERS),
              'files_modified': sorted(changed),
              'preserved': ['species/object IDs', 'all generation settings', 'regional forms',
                            'Primal Reversions', 'base Necrozma', 'existing songs', 'moves/abilities',
                            'custom players/Fly/Surf/Glide', 'maps/events', 'other custom gimmicks'],
              'rom_savings_bytes': None, 'build_status': 'not built', 'mgba_status': 'not tested'}
    return {p: originals[p] for p in changed}, changed, report


def atomic_write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name(path.name + '.kyoto-temp')
    require(not temp.exists(), f'Temporary path exists: {temp}')
    try:
        temp.write_bytes(data)
        if path.exists():
            shutil.copymode(path, temp)
        os.replace(temp, path)
    finally:
        if temp.exists():
            temp.unlink()


def apply_changes(root: Path, before: dict[str, bytes], after: dict[str, bytes], output: Path) -> None:
    # Check every source hash before writing anything.
    for path, old in before.items():
        require((root / path).read_bytes() == old, 'Source changed during planning: ' + path)
    manifest = {'root': str(root), 'version': VERSION, 'files': {}}
    for path, old in before.items():
        f = output / 'originals' / path
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_bytes(old)
        manifest['files'][path] = {'before': digest(old), 'after': digest(after[path])}
    (output / 'backup.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    applied = []
    try:
        for path, new in after.items():
            atomic_write(root / path, new)
            applied.append(path)
    except BaseException:
        for path in applied:
            atomic_write(root / path, before[path])
        raise


def rollback(root: Path, output: Path) -> None:
    manifest = json.loads((output / 'backup.json').read_text(encoding='utf-8'))
    require(Path(manifest['root']).resolve() == root, 'Backup belongs to another source directory')
    for path, hashes in manifest['files'].items():
        current = digest((root / path).read_bytes())
        require(current in (hashes['before'], hashes['after']), 'Refusing to overwrite newer edits: ' + path)
        require(digest((output / 'originals' / path).read_bytes()) == hashes['before'], 'Damaged backup: ' + path)
    for path in manifest['files']:
        atomic_write(root / path, (output / 'originals' / path).read_bytes())


def build(root: Path, output: Path, label: str, jobs: int, make_args: list[str]) -> int:
    cmd = ['make', '-B', '-j' + str(jobs), '-O', 'all'] + make_args
    print('BUILD', label, ':', ' '.join(cmd), flush=True)
    log_path = output / (label + '-build.log')
    tail: collections.deque[str] = collections.deque(maxlen=25)
    with log_path.open('w', encoding='utf-8') as log:
        process = subprocess.Popen(cmd, cwd=root, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True, errors='replace')
        assert process.stdout is not None
        for line in process.stdout:
            log.write(line)
            tail.append(line)
            if ('error:' in line.lower() or 'overflow' in line.lower()
                    or 'ROM:' in line or 'Memory region' in line):
                print(line.rstrip(), flush=True)
        returncode = process.wait()
    if returncode != 0:
        print('Last build messages:', file=sys.stderr)
        print(''.join(tail), file=sys.stderr, end='')
    require(returncode == 0, f'{label} build failed. See {log_path}')
    elf = root / 'pokeemerald.elf'
    require(elf.is_file(), 'Build produced no pokeemerald.elf; custom output names are not supported')
    symbols = subprocess.run(['arm-none-eabi-nm', '-n', str(elf)], check=True, capture_output=True, text=True).stdout
    match = re.search(r'(?m)^([0-9a-fA-F]+)\s+\S\s+__rom_end\s*$', symbols)
    require(match is not None, '__rom_end not found; refusing to estimate from padded .gba size')
    end = int(match[1], 16)
    used = end - ROM_BASE
    require(0 < used <= ROM_CAPACITY, '__rom_end is outside the 32 MiB ROM area')
    for ext in ('gba', 'elf', 'map'):
        src = root / ('pokeemerald.' + ext)
        if src.is_file():
            shutil.copy2(src, output / (label + '.' + ext))
    (output / (label + '-symbols.txt')).write_text(symbols, encoding='utf-8')
    return used


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path, help='PokéKyoto source directory')
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--apply', action='store_true', help='Apply source changes without a ROM build')
    mode.add_argument('--build-compare', action='store_true', help='Build baseline, apply, build optimized, report exact net savings')
    mode.add_argument('--rollback', type=Path, help='Restore source from this output/backup directory')
    parser.add_argument('--output', type=Path, help='New output directory OUTSIDE source; default is a timestamped sibling')
    parser.add_argument('--jobs', type=int, default=min(os.cpu_count() or 2, 4))
    parser.add_argument('--make-arg', action='append', default=[], help='Same make VAR=VALUE override for both builds')
    args = parser.parse_args(argv)
    root = args.root.expanduser().resolve()
    try:
        if args.rollback:
            rollback(root, args.rollback.expanduser().resolve())
            print('Sources restored. Rebuild before testing; your current save was not changed.')
            return 0
        require(args.jobs >= 1, '--jobs must be positive')
        for arg in args.make_arg:
            require(re.match(r'^[A-Za-z_][A-Za-z0-9_]*=[^\n]*$', arg) is not None, 'Only make VAR=VALUE overrides are allowed')
            require(arg.split('=', 1)[0] not in {'BUILD', 'GAME_VERSION', 'FILE_NAME', 'TEST', 'RELEASE'}, 'Do not change the build target')
        print('Installer:', VERSION, flush=True)
        print('Checking local sources; no source changes have been written.', flush=True)
        before, after, report = source_plan(root)
        stamp = dt.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
        output = (args.output or root.parent / ('kyoto-space-result-' + stamp)).expanduser().resolve()
        require(not output.exists(), 'Output already exists: ' + str(output))
        require(output != root and root not in output.parents, 'Output must be outside the source tree')
        output.mkdir(parents=True)
        patch = ''.join(''.join(difflib.unified_diff(before[p].decode('utf-8').splitlines(True),
                        after[p].decode('utf-8').splitlines(True), fromfile='a/' + p, tofile='b/' + p))
                        for p in sorted(after))
        (output / 'changes.patch').write_text(patch, encoding='utf-8')
        (output / 'FRLG_NPC_IDS.txt').write_text('\n'.join(report['frlg_npc_ids']) + '\n', encoding='utf-8')
        report_file = output / 'report.json'
        def save_report() -> None:
            report_file.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
        save_report()
        print(f'Validated source transformation: {len(after)} files, {report["frlg_npc_count"]} FRLG NPCs.')
        print('Report/patch:', output)
        if not (args.apply or args.build_compare):
            print('DRY RUN ONLY: no source files changed. Complete ROM compile and mGBA tests are still required.')
            return 0
        baseline = None
        if args.build_compare:
            for executable in ('make', 'arm-none-eabi-gcc', 'arm-none-eabi-nm'):
                require(shutil.which(executable) is not None, 'Missing required build tool: ' + executable)
            baseline = build(root, output, 'before', args.jobs, args.make_arg)
            report['before_used_bytes'] = baseline
            report['before_free_bytes'] = ROM_CAPACITY - baseline
            save_report()
        apply_changes(root, before, after, output)
        report['source_changes_applied'] = True
        save_report()
        if args.build_compare:
            try:
                optimized = build(root, output, 'after', args.jobs, args.make_arg)
            except BaseException:
                rollback(root, output)
                for ext in ('gba', 'elf', 'map'):
                    src = output / ('before.' + ext)
                    if src.is_file():
                        shutil.copy2(src, root / ('pokeemerald.' + ext))
                report.update(source_changes_applied=False, build_status='optimized build failed; source rolled back')
                save_report()
                print('Optimization was rolled back; see the failed build log.', file=sys.stderr)
                raise
            assert baseline is not None
            report.update(after_used_bytes=optimized, after_free_bytes=ROM_CAPACITY - optimized,
                          rom_savings_bytes=baseline - optimized, build_status='both builds passed')
            save_report()
            print(f'BEFORE: {baseline:,} bytes used; {ROM_CAPACITY - baseline:,} free')
            print(f'AFTER:  {optimized:,} bytes used; {ROM_CAPACITY - optimized:,} free')
            print(f'NET SAVING (including FRLG): {baseline - optimized:,} bytes ({(baseline - optimized)/1048576:.3f} MiB)')
            print('Test ROM:', output / 'after.gba')
            print('Not yet gameplay-tested. Use a COPY of your save for mGBA testing.')
        else:
            print('Source changes applied, NOT ROM-tested. Rebuild and test before normal use.')
        print('Rollback: python3', Path(__file__).resolve(), str(root), '--rollback', str(output))
        return 0
    except (PatchError, OSError, UnicodeError, subprocess.SubprocessError) as exc:
        print('STOP:', exc, file=sys.stderr)
        return 1

if __name__ == '__main__':
    raise SystemExit(main())
