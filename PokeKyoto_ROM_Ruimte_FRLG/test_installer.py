#!/usr/bin/env python3
"""Synthetic unit/integration tests, NOT a PokéKyoto ROM or mGBA test."""
import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import kyoto_optimize as k

NAMES = ['LittleBoyFrlg', 'LittleGirlFrlg', 'YoungsterFrlg', 'BugCatcherFrlg',
         'LassFrlg', 'Woman1Frlg', 'FatManFrlg', 'Woman2Frlg', 'BeautyFrlg',
         'Woman3Frlg', 'OldWomanFrlg', 'CamperFrlg', 'PicnickerFrlg', 'MomFrlg',
         'TuberFFrlg', 'HikerFrlg', 'GentlemanFrlg', 'SailorFrlg', 'NurseFrlg', 'ProfOak']

class Tests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.parent = Path(self.tmp.name)
        self.root = self.parent / 'pokekyoto'
        self.root.mkdir()
    def tearDown(self):
        self.tmp.cleanup()
    def put(self, p, t):
        f = self.root / p
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_text(t, encoding='utf-8')
    def fixture(self):
        self.put('Makefile', 'GAME_VERSION ?= EMERALD\n')
        defs = k.FORMS + tuple('P_FAMILY_' + x for x in k.UB_FAMILIES)
        self.put('include/config/species_enabled.h',
            ''.join('#define ' + x + ' TRUE\n' for x in defs) +
            '#define P_GEN_9_POKEMON TRUE\n#define P_FAMILY_SPRITZEE TRUE\n#define P_PRIMAL_REVERSIONS TRUE\n')
        self.put('include/config/pokemon.h', '#define P_MODIFIED_MEGA_CRIES P_MEGA_EVOLUTIONS\n#define P_CRIES_ENABLED TRUE\n')
        self.put('src/data/gimmicks.h', 'const struct GimmickInfo gGimmicksInfo[] = {\n' +
            ''.join('[GIMMICK_' + x + '] = { .CanActivate = CanSomething, .foo={1,2} },\n' for x in k.GIMMICKS) +
            '[GIMMICK_DOMAIN] = { .CanActivate = CanDomain },\n};\n')
        self.put('src/battle_test.c', '#include <stdbool.h>\ntypedef int bool32;\n#define FALSE 0\nint activated;\n' +
            ''.join('bool32 ' + x + '(int battler)\n{\n return battler + 1;\n}\n' for x in k.CAN_FUNCTIONS) +
            ''.join('void ' + x + '(int battler)\n{\n activated += battler + 1;\n}\n' for x in k.ACTIVATE_FUNCTIONS) +
            'int ActivateDomainExpansion(void) { return 42; }\n')
        p = k.OBJECT_DIR
        self.put(p + 'object_event_graphics.h', '#if IS_FRLG\n' +
            'const unsigned gObjectEventPal_NpcBlue[] = INCGFX_U16("blue.pal", ".gbapal");\n' +
            'const unsigned gObjectEventPal_NpcBlueReflection[] = INCGFX_U16("blue_r.pal", ".gbapal");\n' +
            ''.join('const unsigned gObjectEventPic_' + n + '[] = INCGFX_U32("graphics/object_events/pics/people/' + n + '.png", ".4bpp");\n' for n in NAMES) +
            'const unsigned gObjectEventPic_RedNormal[] = INCGFX_U32("graphics/object_events/pics/people/red/red_normal.png", ".4bpp");\n#endif\n')
        self.put(p + 'object_event_pic_tables.h', '#if IS_FRLG\n' +
            ''.join('static const unsigned *const sPicTable_' + n + '[] = { gObjectEventPic_' + n + ' };\n' for n in NAMES + ['RedNormal']) + '#endif\n')
        self.put(p + 'object_event_graphics_info.h', '#if IS_FRLG\n' +
            ''.join('const struct ObjectEventGraphicsInfo gObjectEventGraphicsInfo_' + n + ' = {\n .images = sPicTable_' + n + ',\n .paletteTag = OBJ_EVENT_PAL_TAG_NPC_BLUE,\n .reflectionPaletteTag = OBJ_EVENT_PAL_TAG_NONE,\n};\n' for n in NAMES + ['RedNormal']) + '#endif\n')
        self.put(p + 'object_event_graphics_info_pointers.h',
            '#if IS_FRLG\n' + ''.join('extern const struct ObjectEventGraphicsInfo gObjectEventGraphicsInfo_' + n + ';\n' for n in NAMES + ['RedNormal']) +
            '#endif\nconst struct ObjectEventGraphicsInfo *const gObjectEventGraphicsInfoPointers[NUM_OBJ_EVENT_GFX] = {\n#if IS_FRLG\n' +
            ''.join('[OBJ_EVENT_GFX_' + n.upper() + '] = &gObjectEventGraphicsInfo_' + n + ',\n' for n in NAMES) +
            '[OBJ_EVENT_GFX_RED_NORMAL] = &gObjectEventGraphicsInfo_RedNormal,\n#endif // IS_FRLG\n};\n')
        self.put('src/event_object_movement.c', 'const struct SpritePalette sObjectEventSpritePalettes[] = {\n#if IS_FRLG\n' +
            '{gObjectEventPal_NpcBlue, OBJ_EVENT_PAL_TAG_NPC_BLUE},\n{gObjectEventPal_NpcBlueReflection, OBJ_EVENT_PAL_TAG_NPC_BLUE_REFLECTION},\n' +
            '#endif // IS_FRLG\n{0, OBJ_EVENT_PAL_TAG_NONE},\n};\n')
        self.put('include/constants/species.h', '// Preserve numeric IDs exactly\n')
        self.put('sound/direct_sound_samples/kyoto_littleroot.wav', 'DO NOT TOUCH THE MUSIC')
        self.put('src/glide.c', '// Preserve custom movement\n')
    def test_mask_preserves_offsets(self):
        s = 'const int x = func(";}", 1); // {\n/*\n} */\n'
        m = k.mask_c(s)
        self.assertEqual(len(m), len(s))
        self.assertEqual(m.count('\n'), s.count('\n'))
        self.assertNotIn('"', m)
    def test_declaration_parser(self):
        s = 'const struct X *const gFoo[] = {\n {1,2}, "};"\n};\n'
        d = k.declarations(s)['gFoo']
        self.assertEqual(d.text, s.rstrip())
        self.assertEqual(s[d.body_end], '}')
    def test_define_and_condition_safety(self):
        self.assertEqual(k.replace_define('#define A TRUE // hi\n', 'A', 'FALSE'), '#define A FALSE // hi\n')
        with self.assertRaises(k.PatchError):
            k.replace_define('#define A 1\n#define A 2\n', 'A', '0')
        source = '#if IS_FRLG\nconst int x = 1;\n#else\nconst int y = 1;\n#endif\n'
        self.assertTrue(k.frlg_only(source, source.index('const int x')))
        self.assertFalse(k.frlg_only(source, source.index('const int y')))
    def test_plan_apply_rollback(self):
        self.fixture()
        original = {p.relative_to(self.root): p.read_bytes() for p in self.root.rglob('*') if p.is_file()}
        before, after, report = k.source_plan(self.root)
        self.assertEqual(report['frlg_npc_count'], 20)
        self.assertNotIn('OBJ_EVENT_GFX_RED_NORMAL', report['frlg_npc_ids'])
        self.assertNotIn('sound/direct_sound_samples/kyoto_littleroot.wav', after)
        self.assertIn(b'GIMMICK_DOMAIN', after['src/data/gimmicks.h'])
        self.assertIn(b'P_FAMILY_SPRITZEE TRUE', after['include/config/species_enabled.h'])
        out = self.parent / 'out'
        out.mkdir()
        k.apply_changes(self.root, before, after, out)
        with self.assertRaises(k.PatchError):
            k.source_plan(self.root)
        k.rollback(self.root, out)
        for p, data in original.items():
            self.assertEqual((self.root / p).read_bytes(), data, str(p))
    def test_newer_edits_are_not_overwritten(self):
        self.fixture()
        before, after, _ = k.source_plan(self.root)
        out = self.parent / 'out'
        out.mkdir()
        k.apply_changes(self.root, before, after, out)
        path = self.root / next(iter(after))
        path.write_bytes(path.read_bytes() + b'// newer edit\n')
        with self.assertRaises(k.PatchError):
            k.rollback(self.root, out)
    def test_domain_reused_gimmick_is_rejected(self):
        self.fixture()
        p = self.root / 'src/data/gimmicks.h'
        text = p.read_text()
        p.write_text(text.replace('.CanActivate = CanSomething', '.CanActivate = CanDomainExpansion', 1))
        original = p.read_bytes()
        with self.assertRaises(k.PatchError):
            k.source_plan(self.root)
        self.assertEqual(p.read_bytes(), original)
    def test_missing_config_does_not_write(self):
        self.fixture()
        p = self.root / 'include/config/species_enabled.h'
        p.write_text(p.read_text().replace('#define P_GIGANTAMAX_FORMS TRUE\n', ''))
        original = {f: f.read_bytes() for f in self.root.rglob('*') if f.is_file()}
        with self.assertRaises(k.PatchError):
            k.source_plan(self.root)
        for f, data in original.items():
            self.assertEqual(f.read_bytes(), data)
    @unittest.skipUnless(shutil.which('gcc'), 'Host GCC not installed')
    def test_generated_declarations_compile_both_game_modes(self):
        self.fixture()
        _, after, report = k.source_plan(self.root)
        for p, b in after.items():
            (self.root / p).write_bytes(b)
        preamble = '#include <stddef.h>\n#define INCGFX_U16(...) {0}\n#define INCGFX_U32(...) {0}\n' + \
            'enum { OBJ_EVENT_PAL_TAG_NONE, OBJ_EVENT_PAL_TAG_NPC_BLUE, OBJ_EVENT_PAL_TAG_NPC_BLUE_REFLECTION };\n' + \
            'enum { ' + ','.join(report['frlg_npc_ids']) + ', OBJ_EVENT_GFX_RED_NORMAL, NUM_OBJ_EVENT_GFX };\n' + \
            'struct ObjectEventGraphicsInfo { const unsigned *const *images; int paletteTag, reflectionPaletteTag; };\n' + \
            'struct SpritePalette { const unsigned *data; int tag; };\n'
        code = preamble + ''.join('#include "' + k.OBJECT_DIR + f + '"\n' for f in
             ['object_event_graphics.h', 'object_event_pic_tables.h', 'object_event_graphics_info.h', 'object_event_graphics_info_pointers.h']) + \
             '#include "src/event_object_movement.c"\nint main(void) { return gObjectEventGraphicsInfoPointers[0] == NULL || sObjectEventSpritePalettes[0].tag == OBJ_EVENT_PAL_TAG_NONE; }\n'
        self.put('probe.c', code)
        for mode in ('0', '1'):
            exe = self.root / ('probe' + mode)
            p = subprocess.run(['gcc', '-std=c17', '-Wall', '-Werror', '-DIS_FRLG=' + mode, 'probe.c', '-o', str(exe)], cwd=self.root, text=True, capture_output=True)
            self.assertEqual(p.returncode, 0, p.stderr)
            self.assertEqual(subprocess.run([str(exe)]).returncode, 0)
    @unittest.skipUnless(shutil.which('gcc'), 'Host GCC not installed')
    def test_gimmick_guards_execute_and_domain_remains(self):
        self.fixture()
        _, after, _ = k.source_plan(self.root)
        code = after['src/battle_test.c'].decode() + '\nint main(void) {\n'
        for name in k.CAN_FUNCTIONS:
            code += f'if ({name}(0)) return 1;\n'
        for name in k.ACTIVATE_FUNCTIONS:
            code += f'{name}(0);\n'
        code += 'return activated != 0 || ActivateDomainExpansion() != 42;\n}\n'
        self.put('gimmick_probe.c', code)
        exe = self.root / 'gimmick_probe'
        p = subprocess.run(['gcc', '-std=c17', '-Wall', '-Werror', 'gimmick_probe.c', '-o', str(exe)], cwd=self.root, text=True, capture_output=True)
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual(subprocess.run([str(exe)]).returncode, 0)


    def add_follower_collision(self):
        """Reproduce the real project layout: legacy and follower static names."""
        legacy = self.root / k.OBJECT_DIR / 'object_event_pic_tables.h'
        legacy.write_text(legacy.read_text() +
            '\n#if IS_FRLG\n'
            'static const struct SpriteFrameImage sPicTable_Pidgey[] = {\n'
            '    overworld_frame(gObjectEventPic_PidgeyOld, 2, 2, 0),\n'
            '};\n#endif\n')
        self.put(k.OBJECT_DIR + 'object_event_pic_tables_followers.h',
            '#if OW_POKEMON_OBJECT_EVENTS\n#if P_FAMILY_PIDGEY\n'
            'static const struct SpriteFrameImage sPicTable_Pidgey[] = {\n'
            '    overworld_ascending_frames(gObjectEventPic_Pidgey, 4, 4),\n'
            '};\n#endif\n#endif\n')

    def test_pidgey_follower_collision_regression(self):
        self.fixture()
        self.add_follower_collision()
        before, after, report = k.source_plan(self.root)
        self.assertEqual(report['frlg_npc_count'], 20)
        follower = k.OBJECT_DIR + 'object_event_pic_tables_followers.h'
        self.assertNotIn(follower, after)
        self.assertNotIn(follower, before)
        self.assertIn('object_event_pic_tables_followers.h', report['excluded_npc_index_headers'])
        pic = after[k.OBJECT_DIR + 'object_event_pic_tables.h'].decode()
        self.assertEqual(pic.count('sPicTable_Pidgey[]'), 1)
        self.assertIn('gObjectEventPic_PidgeyOld', pic)
        self.assertNotIn('overworld_ascending_frames(gObjectEventPic_Pidgey,', pic)

    def test_conflicting_npc_definitions_still_stop(self):
        self.fixture()
        self.put(k.OBJECT_DIR + 'extra_npc.h',
            'static const unsigned *const sPicTable_ProfOak[] = { gOtherOak };\n')
        snapshot = {f: f.read_bytes() for f in self.root.rglob('*') if f.is_file()}
        with self.assertRaisesRegex(k.PatchError, 'Ambiguous NPC object dependency: sPicTable_ProfOak'):
            k.source_plan(self.root)
        for f, data in snapshot.items():
            self.assertEqual(f.read_bytes(), data)

    def test_same_header_duplicates_still_stop(self):
        self.fixture()
        p = self.root / k.OBJECT_DIR / 'object_event_pic_tables.h'
        p.write_text(p.read_text() +
            'static const unsigned *const sPicTable_ProfOak[] = { gOtherOak };\n')
        with self.assertRaisesRegex(k.PatchError, 'Multiple definitions'):
            k.source_plan(self.root)

    def test_follower_headers_remain_identical_after_apply_rollback(self):
        self.fixture()
        self.add_follower_collision()
        self.put(k.OBJECT_DIR + 'object_event_graphics_info_followers.h',
            'const int gFollowerUnrelatedDefinition = 5;\n')
        paths = [self.root / k.OBJECT_DIR / n for n in k.FOLLOWER_HEADERS]
        saved = {p: p.read_bytes() for p in paths}
        before, after, _ = k.source_plan(self.root)
        output = self.parent / 'output'
        output.mkdir()
        k.apply_changes(self.root, before, after, output)
        for p, content in saved.items():
            self.assertEqual(p.read_bytes(), content)
        k.rollback(self.root, output)
        for p, content in saved.items():
            self.assertEqual(p.read_bytes(), content)

    def test_old_patch_marker_is_still_detected(self):
        self.fixture()
        p = self.root / 'src/data/gimmicks.h'
        p.write_text(p.read_text() + '// KYOTO_SPACE_FRLG_V1\n')
        with self.assertRaisesRegex(k.PatchError, 'Patch marker already present'):
            k.source_plan(self.root)

    def test_dry_run_with_follower_collision_does_not_modify_sources(self):
        self.fixture()
        self.add_follower_collision()
        saved = {f: f.read_bytes() for f in self.root.rglob('*') if f.is_file()}
        output = self.parent / 'dry-run'
        self.assertEqual(k.main([str(self.root), '--output', str(output)]), 0)
        self.assertTrue((output / 'changes.patch').is_file())
        for f, content in saved.items():
            self.assertEqual(f.read_bytes(), content)

if __name__ == '__main__':
    unittest.main(verbosity=2)
