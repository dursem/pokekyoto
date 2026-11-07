"""Installer tests using source excerpts/fixtures, not a full game build."""
import contextlib
import copy
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

PKG = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('kyoto_install', PKG / 'install.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

HEADER = '''#ifndef GUARD_BATTLE_BG_H
#define GUARD_BATTLE_BG_H
void DrawMainBattleBackground(void);
void LoadKyotoBattleActionUi(void);
#endif
'''
BACKGROUND = '''#include "global.h"
#include "battle.h"
#include "battle_anim.h"
#include "battle_bg.h"
#include "battle_environment.h"

static const struct WindowTemplate sStandardBattleWindowTemplates[] =
{
    [B_WIN_ACTION_NAME] = {
        .bg = 0,
        .tilemapLeft = 1,
        .tilemapTop = 37,
        .width = 11,
        .height = 2,
        .paletteNum = 12,
        .baseBlock = 0x0170,
    },
};

static u8 GetBattleEnvironmentOverride(void)
{
    u8 battleScene = GetCurrentMapBattleScene();
    if (battleScene == MAP_BATTLE_SCENE_NORMAL)
        return gBattleEnvironment;
    return GetBattleEnvironmentByMapScene(battleScene);
}

void BattleInitBgsAndWindows(void)
{
    InitWindows(gBattleWindowTemplates[gBattleScripting.windowsType]);
}
'''
ANIM = '''#include "global.h"
#include "battle.h"
#include "battle_anim.h"
#include "battle_interface.h"
#include "contest.h"
const struct UCoords8 sBattlerCoords[2][4] = {
    {{72, 80}, {176, 40}, {48, 40}, {112, 80}},
    {{32, 80}, {200, 40}, {90, 84}, {152, 32}},
};
u8 GetBattlerSpriteCoord(enum BattlerId battler, u8 coordType)
{
    u8 retVal;
    switch (coordType)
    {
    case BATTLER_COORD_X:
    case BATTLER_COORD_X_2:
        retVal = sBattlerCoords[GetBattlerCoordsIndex(battler)][GetBattlerPosition(battler)].x;
        break;
    case BATTLER_COORD_Y:
        retVal = sBattlerCoords[GetBattlerCoordsIndex(battler)][GetBattlerPosition(battler)].y;
        break;
    default:
        retVal = GetBattlerSpriteFinal_Y(battler, 0, 1);
    }
    return retVal;
}
'''
CONTROLLERS = '''#include "global.h"
#include "battle.h"
#include "battle_anim.h"
#include "battle_interface.h"

void BtlController_HandleDrawTrainerPic(enum BattlerId battler, enum TrainerPicID trainerPicId, bool32 isFrontPic, s16 xPos, s16 yPos, s32 subpriority)
{
    if (!IsOnPlayerSide(battler))
    {
        gBattleStruct->trainerSlideSpriteIds[battler] = CreateSprite(&gMultiuseSpriteTemplate,
                                                   xPos,
                                                   yPos,
                                                   subpriority);
    }
}

void BtlController_HandleTrainerSlide(enum BattlerId battler, enum TrainerPicID trainerPicId)
{
    if (IsOnPlayerSide(battler))
    {
        gBattleStruct->trainerSlideSpriteIds[battler] = CreateSprite(&gMultiuseSpriteTemplate,
                                                         80,
                                                         (8 - GetTrainerBackPicCoords(trainerPicId)->size) * 4 + 80,
                                                         30);
        gSprites[gBattleStruct->trainerSlideSpriteIds[battler]].sSpeedX = 2;
    }
    else
    {
        gBattleStruct->trainerSlideSpriteIds[battler] = CreateSprite(&gMultiuseSpriteTemplate, 176, 40, 0);
        gSprites[gBattleStruct->trainerSlideSpriteIds[battler]].x += 32;
        gSprites[gBattleStruct->trainerSlideSpriteIds[battler]].sSpeedX = -2;
    }
}

void Unrelated(void)
{
    CreateSprite(&gMultiuseSpriteTemplate, 176, 40, 0);
}
'''
MESSAGE = '''#include "global.h"
#include "window.h"
void BattlePutTextOnWindow(const u8 *text, u8 windowId)
{
    if (windowId & B_WIN_COPYTOVRAM)
    {
        windowId &= ~B_WIN_COPYTOVRAM;
        copyToVram = FALSE;
    }
    else
    {
        if (windowId == B_WIN_MSG && gBattleScripting.windowsType == B_WIN_TYPE_NORMAL)
            LoadKyotoBattleMessageUi();

''' + m.BORDER_OLD + '''
            FillWindowPixelBuffer(windowId, textInfo[windowId].fillValue);
        copyToVram = TRUE;
    }
    printerTemplate.x = textInfo[windowId].x;
    printerTemplate.y = textInfo[windowId].y;
}
'''
SOURCES = dict(zip(m.CODE_PATHS, (HEADER, BACKGROUND, ANIM, CONTROLLERS, MESSAGE)))

class CodeTests(unittest.TestCase):
    def test_all_five_sources_transform(self):
        for path, src in SOURCES.items():
            self.assertNotEqual(m.transform_code(path, src), src)

    def test_all_five_idempotent(self):
        for path, src in SOURCES.items():
            out = m.transform_code(path, src)
            self.assertEqual(out, m.transform_code(path, out))

    def test_helper_includes_interface_declaration(self):
        out = m.transform_code('src/battle_bg.c', BACKGROUND)
        self.assertIn('#include "battle_interface.h"', out)
        self.assertIn('#include "contest.h"', out)
        self.assertGreater(out.index(m.CENTER_HELPER), out.index('static u8 GetBattleEnvironmentOverride'))

    def test_existing_include_not_duplicated(self):
        src = BACKGROUND.replace('#include "battle_bg.h"', '#include "battle_bg.h"\n#include "contest.h"')
        out = m.transform_code('src/battle_bg.c', src)
        self.assertEqual(out.count('#include "contest.h"'), 1)

    def test_unrelated_local_work_preserved(self):
        extra = '\n// My custom Domain Expansion implementation\nvoid MyDomain(void) { custom(); }\n'
        for path, src in SOURCES.items():
            self.assertTrue(m.transform_code(path, src + extra).endswith(extra))

    def test_only_x_lookup_changes(self):
        out = m.transform_code('src/battle_anim_mons.c', ANIM)
        self.assertIn('retVal = sBattlerCoords[GetBattlerCoordsIndex(battler)][GetBattlerPosition(battler)].y;', out)
        self.assertIn('{{72, 80}, {176, 40}, {48, 40}, {112, 80}}', out)
        self.assertIn('{{32, 80}, {200, 40}, {90, 84}, {152, 32}}', out)

    def test_trainer_slide_preserves_other_calls_and_y(self):
        out = m.transform_code('src/battle_controllers.c', CONTROLLERS)
        self.assertIn('void Unrelated(void)\n{\n    CreateSprite(&gMultiuseSpriteTemplate, 176, 40, 0);', out)
        self.assertIn('KyotoBattlePlatformX(battler, 208) - 32, 40, 0', out)
        self.assertIn('.x += 32;', out)
        self.assertIn('(8 - GetTrainerBackPicCoords(trainerPicId)->size) * 4 + 80', out)

    def test_unexpected_slide_compensation_rejected(self):
        with self.assertRaises(m.PatchError):
            m.transform_code('src/battle_controllers.c', CONTROLLERS.replace('.x += 32;', '.x += 20;'))

    def test_name_text_origin_preserved(self):
        out = m.transform_code('src/battle_message.c', MESSAGE)
        self.assertIn('printerTemplate.x = textInfo[windowId].x;', out)
        self.assertIn('printerTemplate.y = textInfo[windowId].y;', out)
        self.assertIn('FillWindowPixelBuffer(windowId, textInfo[windowId].fillValue);', out)

    def test_changed_name_width_rejected(self):
        with self.assertRaises(m.PatchError):
            m.transform_code('src/battle_bg.c', BACKGROUND.replace('.width = 11', '.width = 12'))

    def test_changed_name_origin_rejected(self):
        with self.assertRaises(m.PatchError):
            m.transform_code('src/battle_bg.c', BACKGROUND.replace('.tilemapLeft = 1', '.tilemapLeft = 2'))

    def test_unknown_existing_helper_rejected(self):
        with self.assertRaises(m.PatchError):
            m.transform_code('src/battle_bg.c', BACKGROUND + '\ns16 KyotoBattlePlatformX(u32 b, s16 x) { return x; }\n')

    def test_ambiguous_function_rejected(self):
        with self.assertRaises(m.PatchError):
            m.transform_code('src/battle_anim_mons.c', ANIM + ANIM)

    def test_comment_braces_ignored(self):
        src = MESSAGE.replace('    if (windowId &', '    /* { ignored } } */\n    if (windowId &')
        self.assertIn(m.BORDER_NEW, m.transform_code('src/battle_message.c', src))

    def test_unknown_fill_refused(self):
        with self.assertRaises(m.PatchError):
            m.transform_code('src/battle_message.c', MESSAGE.replace('PIXEL_FILL(1)', 'PIXEL_FILL(2)'))

class TransactionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'pokekyoto'
        self.root.mkdir()
        self.patchers = []
        for path, text in SOURCES.items():
            p = self.root / path
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(text)
        (self.root / 'Makefile').write_text('GAME_VERSION ?= EMERALD\nall:\n\t@echo example\n')
        gfx = self.root / 'src/data/graphics/battle_environment.h'
        gfx.parent.mkdir(parents=True, exist_ok=True)
        gfx.write_text('\n'.join(
            f'const u32 a_{env}_{ext}[] = INCGFX_U32("graphics/battle_environment/kyoto_{env}/{suffix}", "{fmt}");'
            for env in ('grass', 'land') for ext,suffix,fmt in [('tiles','tiles.png','.4bpp.smol'),('map','map.bin','.smolTM')]))
        inputs = {}
        for path in m.ASSET_INPUT_GIT_SHA1:
            p = self.root / path
            p.parent.mkdir(parents=True, exist_ok=True)
            b = ('fixture original ' + path).encode()
            p.write_bytes(b)
            inputs[path] = m.git_blob_sha1(b)
        p = mock.patch.dict(m.ASSET_INPUT_GIT_SHA1, inputs)
        p.start(); self.addCleanup(p.stop)
        pals = {}
        for path in m.PALETTE_INPUT_GIT_SHA1:
            p = self.root / path
            p.write_bytes(b'fixture palette')
            pals[path] = m.git_blob_sha1(p.read_bytes())
        p = mock.patch.dict(m.PALETTE_INPUT_GIT_SHA1, pals)
        p.start(); self.addCleanup(p.stop)
        p = mock.patch.object(m, 'running_builds', return_value=[])
        p.start(); self.addCleanup(p.stop)
        (self.root / 'include/config').mkdir()
        (self.root / 'include/config/species_enabled.h').write_text('#define P_MEGA_EVOLUTIONS FALSE\n')
        (self.root / 'sound').mkdir()
        (self.root / 'sound/keep.wav').write_bytes(b'MUSIC_UNCHANGED')
        (self.root / 'pokeemerald.gba').write_bytes(b'LOCAL_EXISTING_ROM')
        (self.root / 'pokeemerald.map').write_text('  0x09df8e78 __rom_end = .\n')
        self.quiet = contextlib.redirect_stdout(io.StringIO())
        self.quiet.__enter__(); self.addCleanup(self.quiet.__exit__, None, None, None)

    def snapshot(self):
        return {p.relative_to(self.root).as_posix(): p.read_bytes() for p in self.root.rglob('*') if p.is_file()}

    def test_check_has_no_writes(self):
        before = self.snapshot()
        a,b = m.plan(self.root)
        self.assertEqual(self.snapshot(), before)
        self.assertEqual(len([p for p in a if a[p] != b[p]]), 9)

    def test_exact_nine_file_scope(self):
        a,b = m.plan(self.root)
        self.assertEqual(set(b), set(m.ALL_PATHS))
        self.assertFalse(any('config/' in p or 'sound/' in p or 'object_events' in p for p in b))

    def test_apply_backup_and_restore_roundtrip(self):
        start = self.snapshot(); a,b = m.plan(self.root)
        backup = m.apply(self.root, a, b)
        self.assertIsNotNone(backup)
        self.assertEqual((backup / 'build_before/pokeemerald.gba').read_bytes(), start['pokeemerald.gba'])
        self.assertEqual((self.root / 'include/config/species_enabled.h').read_bytes(), start['include/config/species_enabled.h'])
        m.restore(self.root, backup)
        self.assertEqual(self.snapshot(), start)

    def test_apply_twice_no_second_backup(self):
        a,b = m.plan(self.root); m.apply(self.root, a,b)
        a,b = m.plan(self.root)
        self.assertEqual(a,b)
        self.assertIsNone(m.apply(self.root,a,b))

    def test_preserve_optimization_and_audio(self):
        start = self.snapshot(); a,b = m.plan(self.root); m.apply(self.root,a,b)
        after = self.snapshot()
        for path in start:
            if path not in m.ALL_PATHS:
                self.assertEqual(start[path],after[path],path)

    def test_restore_refuses_new_user_work(self):
        a,b=m.plan(self.root); backup=m.apply(self.root,a,b)
        p=self.root/'src/battle_message.c';p.write_bytes(p.read_bytes()+b'\n// NEW WORK\n')
        before=self.snapshot()
        with self.assertRaises(m.PatchError):m.restore(self.root,backup)
        self.assertEqual(self.snapshot(),before)

    def test_changed_asset_stops_before_any_write(self):
        (self.root/'graphics/battle_environment/kyoto_land/tiles.png').write_bytes(b'MY_NEW_CITY')
        before=self.snapshot()
        with self.assertRaises(m.PatchError):m.plan(self.root)
        self.assertEqual(self.snapshot(),before)

    def test_changed_palette_rejected(self):
        (self.root/'graphics/battle_environment/kyoto_grass/palette.pal').write_bytes(b'MY_COLORS')
        with self.assertRaises(m.PatchError):m.plan(self.root)

    def test_changed_pipeline_rejected(self):
        p=self.root/'src/data/graphics/battle_environment.h';p.write_text(p.read_text().replace('.smolTM','.lz'))
        with self.assertRaises(m.PatchError):m.plan(self.root)

    def test_symlink_source_rejected(self):
        p=self.root/'src/battle_bg.c';data=p.read_bytes();p.unlink()
        outside=Path(self.temp.name)/'outside.c';outside.write_bytes(data);p.symlink_to(outside)
        with self.assertRaises(m.PatchError):m.plan(self.root)
        self.assertEqual(outside.read_bytes(),data)

    def test_snapshot_change_prevents_apply(self):
        a,b=m.plan(self.root)
        p=self.root/'src/battle_bg.c';p.write_bytes(p.read_bytes()+b'\n// LOCAL EDIT\n')
        before=self.snapshot()
        with self.assertRaises(m.PatchError):m.apply(self.root,a,b)
        self.assertEqual(self.snapshot(),before)

    def test_write_failure_rolls_back(self):
        a,b=m.plan(self.root);start=self.snapshot();real=m.atomic_write;once=[False]
        def fail(path,data,mode=None):
            if path==self.root/'src/battle_anim_mons.c' and not once[0]:
                once[0]=True;raise OSError('simulated write failure')
            return real(path,data,mode)
        with mock.patch.object(m,'atomic_write',side_effect=fail):
            with self.assertRaises(OSError):m.apply(self.root,a,b)
        self.assertEqual(self.snapshot(),start)

    def test_keyboardinterrupt_rolls_back(self):
        a,b=m.plan(self.root);start=self.snapshot();real=m.atomic_write;once=[False]
        def fail(path,data,mode=None):
            if path==self.root/'src/battle_controllers.c' and not once[0]:
                once[0]=True;raise KeyboardInterrupt()
            return real(path,data,mode)
        with mock.patch.object(m,'atomic_write',side_effect=fail):
            with self.assertRaises(KeyboardInterrupt):m.apply(self.root,a,b)
        self.assertEqual(self.snapshot(),start)

    def test_busy_build_rejected(self):
        a,b=m.plan(self.root);start=self.snapshot()
        with mock.patch.object(m,'running_builds',return_value=[('12','make')]):
            with self.assertRaises(m.PatchError):m.apply(self.root,a,b)
        self.assertEqual(self.snapshot(),start)

    def test_backup_corruption_prevents_restore(self):
        a,b=m.plan(self.root);backup=m.apply(self.root,a,b)
        (backup/'original/src/battle_bg.c').write_bytes(b'corrupt')
        before=self.snapshot()
        with self.assertRaises(m.PatchError):m.restore(self.root,backup)
        self.assertEqual(self.snapshot(),before)

    def test_backup_path_traversal_prevented(self):
        a,b=m.plan(self.root);backup=m.apply(self.root,a,b)
        p=backup/'manifest.json';v=json.loads(p.read_text());v['files']['../outside']={};p.write_text(json.dumps(v))
        before=self.snapshot()
        with self.assertRaises(m.PatchError):m.restore(self.root,backup)
        self.assertEqual(self.snapshot(),before)

    def test_crlf_preserved(self):
        p=self.root/'src/battle_bg.c';p.write_bytes(p.read_bytes().replace(b'\n',b'\r\n'))
        a,b=m.plan(self.root)
        self.assertIn(b'\r\n',b['src/battle_bg.c'])
        self.assertNotIn(b'\n',b['src/battle_bg.c'].replace(b'\r\n',b''))

    def test_rom_measurement(self):
        self.assertEqual(m.rom_used(self.root),31428216)

    def test_generated_diff_git_apply_check(self):
        if not __import__('shutil').which('git'):self.skipTest('git not installed')
        a,b=m.plan(self.root);backup=m.apply(self.root,a,b);m.restore(self.root,backup)
        diff=(backup/'source_changes.patch').read_text()
        # Binary payload annotations are comments; review/apply those four files through installer.
        p=Path(self.temp.name)/'code.patch';p.write_text(diff)
        r=subprocess.run(['git','apply','--check',str(p)],cwd=self.root,capture_output=True,text=True)
        self.assertEqual(r.returncode,0,r.stdout+r.stderr)

class PayloadTests(unittest.TestCase):
    def test_payload_hashes(self):
        for path,digest in m.ASSET_OUTPUT_SHA256.items():
            self.assertEqual(m.sha256((PKG/'payload'/path).read_bytes()),digest)

    def test_no_forced_build_or_rom_addresses_in_installer(self):
        src=(PKG/'install.py').read_text()
        self.assertNotIn("['make'",src)
        self.assertNotIn('git reset',src)
        self.assertNotIn('0x080457',src)

if __name__=='__main__':unittest.main(verbosity=2)
