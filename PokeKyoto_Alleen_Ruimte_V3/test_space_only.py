#!/usr/bin/env python3
"""Synthetic installer/rollback tests. NOT a complete ROM or mGBA test."""
import contextlib
import io
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock
sys.path.insert(0, str(Path(__file__).resolve().parent))
import kyoto_space_only as k

class Tests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.parent = Path(self.tmp.name)
        self.root = self.parent / 'pokekyoto'
        self.root.mkdir()
        self.fixture()
    def tearDown(self):
        self.tmp.cleanup()
    def put(self, name, content):
        p = self.root / name
        p.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(content, bytes):
            p.write_bytes(content)
        else:
            p.write_text(content, encoding='utf-8')
        return p
    def fixture(self):
        self.put('Makefile', 'GAME_VERSION ?= EMERALD\n')
        self.put('include/config/species_enabled.h',
                 ''.join('#define ' + n + ' TRUE\n' for n in k.FORMS) +
                 ''.join('#define P_FAMILY_' + n + ' P_GEN_7_POKEMON\n' for n in k.UB_FAMILIES) +
                 ''.join('#define P_GEN_' + str(n) + '_POKEMON TRUE\n' for n in range(1, 10)) +
                 '#define P_FAMILY_SPRITZEE P_GEN_6_POKEMON\n#define P_FAMILY_NECROZMA P_GEN_7_POKEMON\n'
                 '#define P_PRIMAL_REVERSIONS TRUE\n#define P_REGIONAL_FORMS TRUE\n')
        self.put('include/config/pokemon.h', '#define P_MODIFIED_MEGA_CRIES P_MEGA_EVOLUTIONS\n#define P_CRIES_ENABLED TRUE\n')
        self.put('src/data/gimmicks.h', 'const struct GimmickInfo gGimmicksInfo[] = {\n' +
                 ''.join('[GIMMICK_' + n + '] = { .CanActivate = CanSomething, .nested = {1,2} },\n' for n in k.GIMMICKS) +
                 '[GIMMICK_DOMAIN] = { .CanActivate = CanDomainExpansion },\n};\n')
        self.put('src/battle_test.c', '#include <stdbool.h>\ntypedef int bool32;\n#define FALSE 0\nint activated;\n' +
                 ''.join('bool32 ' + n + '(int battler)\n{\n return battler + 1;\n}\n' for n in k.CAN_FUNCTIONS) +
                 ''.join('void ' + n + '(int battler)\n{\n activated += battler + 1;\n}\n' for n in k.ACTIVATE_FUNCTIONS) +
                 'int ActivateDomainExpansion(void) { return 42; }\n')
        # Same symbol in distinct source contexts: this must never be parsed/edited.
        self.put('src/data/object_events/object_event_pic_tables.h', '#if IS_FRLG\nconst int sPicTable_Pidgey[] = {1};\n#endif\n')
        self.put('src/data/object_events/object_event_pic_tables_followers.h', '#if OW_POKEMON_OBJECT_EVENTS\nconst int sPicTable_Pidgey[] = {2};\n#endif\n')
        self.put('src/event_object_movement.c', '// custom overworld movement\n')
        self.put('src/glide.c', '// custom glide\n')
        self.put('include/constants/species.h', 'enum Species { SPECIES_SPRITZEE=682, SPECIES_NIHILEGO=793 };\n')
        self.put('include/constants/event_objects.h', 'enum { OBJ_EVENT_GFX_RED, OBJ_EVENT_GFX_LEAF };\n')
        self.put('src/data/pokemon/species_info/gen_6_families.h', '// Jestrick and Jokequinn remain\n')
        self.put('sound/direct_sound_samples/kyoto_littleroot.wav', b'RIFF-original-audio')
        self.put('data/kyoto_audio/route1_pcm.bin', b'original-route1')
        self.put('data/maps/LittlerootTown/map.json', '{"music":"MUS_KYOTO_LITTLEROOT"}')
        self.put('graphics/object_events/pics/people/player.png', b'custom-player-png')
        self.put('pokeemerald.sav', b'YOUR SAVE MUST NEVER CHANGE')
        for name in k.ARTIFACTS:
            self.put(name, ('ENTRY-' + name).encode())
    def snapshot(self):
        return {p.relative_to(self.root).as_posix(): p.read_bytes() for p in self.root.rglob('*') if p.is_file()}
    def plan(self):
        return k.source_plan(self.root)
    def output(self, name='out'):
        p = self.parent / name
        p.mkdir()
        return p
    def fake_build(self, label, used):
        for name in k.ARTIFACTS:
            self.put(name, (label + ':' + name).encode())
        return used
    def compare(self, builder):
        before, after, report = self.plan()
        out = self.output()
        def build(root, output, label, jobs, make_args):
            result = builder(label)
            k.snapshot_artifacts(root, output / (label + '-artifacts'))
            return result
        with mock.patch.object(k, 'build', side_effect=build):
            k.compare_builds(self.root, out, before, after, report, 2, [])
        return before, after, report, out

    def test_no_overworld_or_music_or_id_edits(self):
        before, after, report = self.plan()
        self.assertEqual(set(after), {'include/config/species_enabled.h', 'include/config/pokemon.h',
                                     'src/data/gimmicks.h', 'src/battle_test.c'})
        self.assertFalse(report['frlg_added'])
        self.assertEqual(report['frlg_npc_count'], 0)
        self.assertNotIn('src/event_object_movement.c', after)
        self.assertFalse(any(p.startswith(('sound/', 'graphics/', 'data/', 'include/constants/')) for p in after))
    def test_disabled_forms_ubs_mega_cries(self):
        _, after, _ = self.plan()
        for name in k.FORMS + tuple('P_FAMILY_' + n for n in k.UB_FAMILIES):
            self.assertIn(('#define ' + name + ' FALSE').encode(), after['include/config/species_enabled.h'])
        self.assertIn(b'#define P_MODIFIED_MEGA_CRIES FALSE', after['include/config/pokemon.h'])
    def test_generations_regular_cries_and_fakemon_kept(self):
        _, after, _ = self.plan()
        for n in range(1, 10):
            self.assertIn(f'#define P_GEN_{n}_POKEMON TRUE'.encode(), after['include/config/species_enabled.h'])
        for line in (b'#define P_FAMILY_SPRITZEE P_GEN_6_POKEMON', b'#define P_PRIMAL_REVERSIONS TRUE',
                     b'#define P_REGIONAL_FORMS TRUE', b'#define P_FAMILY_NECROZMA P_GEN_7_POKEMON'):
            self.assertIn(line, after['include/config/species_enabled.h'])
        self.assertIn(b'#define P_CRIES_ENABLED TRUE', after['include/config/pokemon.h'])
    def test_pidgey_collision_does_not_matter(self):
        # No dependency lookup can reintroduce the earlier FRLG error.
        self.put('src/data/object_events/extra.h', 'const int sPicTable_Pidgey[] = {3};\n')
        self.plan()
    def test_apply_and_rollback_bit_exact(self):
        saved = self.snapshot()
        before, after, _ = self.plan()
        out = self.output()
        k.apply_changes(self.root, before, after, out)
        with self.assertRaises(k.PatchError):
            self.plan()
        k.rollback(self.root, out)
        self.assertEqual(saved, self.snapshot())
    def test_unrelated_files_and_save_unchanged_after_apply(self):
        saved = self.snapshot()
        before, after, _ = self.plan()
        k.apply_changes(self.root, before, after, self.output())
        for path, content in saved.items():
            if path not in after:
                self.assertEqual((self.root / path).read_bytes(), content, path)
    def test_dry_run_no_source_change(self):
        saved = self.snapshot()
        with contextlib.redirect_stdout(io.StringIO()):
            ret = k.main([str(self.root), '--output', str(self.parent / 'dry')])
        self.assertEqual(ret, 0)
        self.assertEqual(saved, self.snapshot())
        self.assertTrue((self.parent / 'dry/changes.patch').exists())
    def test_already_applied_frlg_rejected_without_changes(self):
        self.put('src/data/object_events/applied.h', '// ' + k.OLD_MARKER + '\n')
        saved = self.snapshot()
        with self.assertRaisesRegex(k.PatchError, 'earlier FRLG patch'):
            self.plan()
        self.assertEqual(saved, self.snapshot())
    def test_old_installer_folder_is_not_an_applied_frlg_patch(self):
        self.put('PokeKyoto_ROM_Ruimte_FRLG/kyoto_optimize.py', k.OLD_MARKER)
        self.plan()
    def test_missing_define_rejects_without_writes(self):
        p = self.root / 'include/config/species_enabled.h'
        p.write_text(p.read_text().replace('#define P_GIGANTAMAX_FORMS TRUE\n', ''))
        saved = self.snapshot()
        with self.assertRaises(k.PatchError):
            self.plan()
        self.assertEqual(saved, self.snapshot())
    def test_duplicate_definition_rejected(self):
        p = self.root / 'include/config/pokemon.h'
        p.write_text(p.read_text() + '#define P_MODIFIED_MEGA_CRIES TRUE\n')
        with self.assertRaises(k.PatchError):
            self.plan()
    def test_custom_domain_entry_preserved(self):
        _, after, _ = self.plan()
        self.assertIn(b'[GIMMICK_DOMAIN] = { .CanActivate = CanDomainExpansion }', after['src/data/gimmicks.h'])
        self.assertIn(b'int ActivateDomainExpansion(void) { return 42; }', after['src/battle_test.c'])
    def test_domain_reused_gimmick_table_refused(self):
        p = self.root / 'src/data/gimmicks.h'
        p.write_text(p.read_text().replace('CanSomething', 'CanDomainExpansion', 1))
        with self.assertRaisesRegex(k.PatchError, 'Domain Expansion'):
            self.plan()
    def test_domain_reused_function_refused(self):
        p = self.root / 'src/battle_test.c'
        p.write_text(p.read_text().replace('return battler + 1;', 'return DomainExpansion(battler);', 1))
        with self.assertRaisesRegex(k.PatchError, 'Domain Expansion'):
            self.plan()
    def test_apply_rejects_changed_source(self):
        before, after, _ = self.plan()
        path = next(iter(before))
        self.put(path, before[path] + b'// later edit\n')
        saved = self.snapshot()
        with self.assertRaisesRegex(k.PatchError, 'Source changed'):
            k.apply_changes(self.root, before, after, self.output())
        self.assertEqual(saved, self.snapshot())
    def test_rollback_preserves_newer_user_edits(self):
        before, after, _ = self.plan()
        out = self.output()
        k.apply_changes(self.root, before, after, out)
        path = next(iter(before))
        self.put(path, after[path] + b'// later edit\n')
        saved = self.snapshot()
        with self.assertRaisesRegex(k.PatchError, 'newer edits'):
            k.rollback(self.root, out)
        self.assertEqual(saved, self.snapshot())
    @unittest.skipUnless(shutil.which('gcc'), 'Host C compiler missing')
    def test_gimmick_entrypoints_disabled_but_domain_runs(self):
        _, after, _ = self.plan()
        code = after['src/battle_test.c'].decode() + '\nint main(void) {\n'
        for name in k.CAN_FUNCTIONS:
            code += f'if ({name}(0)) return 1;\n'
        for name in k.ACTIVATE_FUNCTIONS:
            code += f'{name}(0);\n'
        code += 'return activated != 0 || ActivateDomainExpansion() != 42; }\n'
        p = self.put('probe.c', code)
        exe = self.root / 'probe'
        result = subprocess.run(['gcc', '-std=c17', '-O2', '-Wall', '-Werror', str(p), '-o', str(exe)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(subprocess.run([str(exe)]).returncode, 0)
    def test_smaller_build_accepted(self):
        saved = self.snapshot()
        with contextlib.redirect_stdout(io.StringIO()):
            before, after, report, out = self.compare(lambda label: self.fake_build(label, 10000 if label == 'before' else 8000))
        self.assertEqual(report['rom_savings_bytes'], 2000)
        self.assertTrue(report['source_changes_applied'])
        self.assertEqual((self.root / 'pokeemerald.gba').read_bytes(), b'candidate:pokeemerald.gba')
        self.assertTrue((out / 'optimized.gba').exists())
        for path, content in saved.items():
            if path not in after and path not in k.ARTIFACTS:
                self.assertEqual((self.root / path).read_bytes(), content, path)
    def test_equal_size_is_not_growth(self):
        with contextlib.redirect_stdout(io.StringIO()):
            _, _, report, out = self.compare(lambda label: self.fake_build(label, 10000))
        self.assertEqual(report['rom_savings_bytes'], 0)
        self.assertTrue((out / 'optimized.gba').exists())
    def test_larger_build_rolled_back(self):
        saved = self.snapshot()
        with contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaisesRegex(k.PatchError, 'LARGER'):
                self.compare(lambda label: self.fake_build(label, 10000 if label == 'before' else 11000))
        for path, content in saved.items():
            expected = ('before:' + path).encode() if path in k.ARTIFACTS else content
            self.assertEqual((self.root / path).read_bytes(), expected, path)
        self.assertFalse((self.parent / 'out/optimized.gba').exists())
        report = json.loads((self.parent / 'out/report.json').read_text())
        self.assertFalse(report['source_changes_applied'])
    def test_baseline_interrupt_restores_entry_rom_no_source_changes(self):
        saved = self.snapshot()
        def fake(label):
            self.put('pokeemerald.gba', b'partial')
            raise KeyboardInterrupt
        with contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises(KeyboardInterrupt):
                self.compare(fake)
        self.assertEqual(saved, self.snapshot())
    def test_baseline_failure_restores_entry_rom(self):
        saved = self.snapshot()
        def fake(label):
            (self.root / 'pokeemerald.elf').unlink()
            raise k.PatchError('compiler error')
        with contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises(k.PatchError):
                self.compare(fake)
        self.assertEqual(saved, self.snapshot())
    def test_candidate_interrupt_rolls_back(self):
        saved = self.snapshot()
        def fake(label):
            if label == 'before':
                return self.fake_build(label, 10000)
            self.put('pokeemerald.gba', b'partial-optimized')
            raise KeyboardInterrupt
        with contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises(KeyboardInterrupt):
                self.compare(fake)
        for path, content in saved.items():
            expected = ('before:' + path).encode() if path in k.ARTIFACTS else content
            self.assertEqual((self.root / path).read_bytes(), expected, path)
    def test_candidate_compile_failure_rolls_back(self):
        saved = self.snapshot()
        def fake(label):
            if label == 'before':
                return self.fake_build(label, 10000)
            raise k.PatchError('linker error')
        with contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises(k.PatchError):
                self.compare(fake)
        for path, content in saved.items():
            expected = ('before:' + path).encode() if path in k.ARTIFACTS else content
            self.assertEqual((self.root / path).read_bytes(), expected, path)
    def test_build_log_stream_is_real_and_nonzero_exit_errors(self):
        log = self.parent / 'test-build.log'
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(k.PatchError):
                k.run_logged([sys.executable, '-c', 'print("compiler error: deliberate failure"); exit(2)'], self.root, log, 'test')
        self.assertIn('compiler error: deliberate failure', log.read_text())
    def test_build_subprocess_is_stopped_on_interrupt(self):
        # Real subprocess + descendant, no real project/toolchain.
        helper = self.parent / 'interrupt_driver.py'
        module_dir = str(Path(k.__file__).parent)
        helper.write_text('import sys, signal\nfrom pathlib import Path\nsys.path.insert(0, ' + repr(module_dir) + ')\n'
            'import kyoto_space_only as k\ntry:\n'
            ' k.run_logged([sys.executable, "-c", '
            + repr('import os, subprocess, sys, time; print("READY", os.getpid(), flush=True); p=subprocess.Popen([sys.executable,"-c","import time; time.sleep(60)"]); print("CHILD", p.pid, flush=True); time.sleep(60)')
            + '], Path(' + repr(str(self.root)) + '), Path(' + repr(str(self.parent / 'interrupt.log')) + '), "test")\n'
            'except KeyboardInterrupt:\n sys.exit(130)\n')
        process = subprocess.Popen([sys.executable, str(helper)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
        pids = []
        try:
            end = time.monotonic() + 8
            log = self.parent / 'interrupt.log'
            while time.monotonic() < end:
                if log.exists() and 'CHILD ' in log.read_text():
                    for line in log.read_text().splitlines():
                        if line.startswith(('READY ', 'CHILD ')):
                            pids.append(int(line.split()[1]))
                    break
                time.sleep(0.05)
            self.assertEqual(len(pids), 2)
            process.send_signal(signal.SIGINT)
            stdout, stderr = process.communicate(timeout=12)
            self.assertEqual(process.returncode, 130, stderr.decode())
            for pid in pids:
                proc = Path('/proc') / str(pid)
                if proc.exists():
                    # A terminated descendant may briefly be a zombie awaiting init.
                    self.assertEqual((proc / 'stat').read_text().split()[2], 'Z')
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            if process.stdout:
                process.stdout.close()
            if process.stderr:
                process.stderr.close()
    def test_no_unmeasured_apply_option(self):
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            k.main([str(self.root), '--apply'])
    def test_measure_uses_linker_symbol_not_padded_size(self):
        self.put('pokeemerald.gba', b'\xff' * 1024)
        result = subprocess.CompletedProcess([], 0, '08000200 A __rom_end\n', '')
        with mock.patch.object(k.subprocess, 'run', return_value=result):
            used, _ = k.measure_rom(self.root)
        self.assertEqual(used, 512)
    def test_invalid_linker_symbol_is_rejected(self):
        result = subprocess.CompletedProcess([], 0, '09000000 A something_else\n', '')
        with mock.patch.object(k.subprocess, 'run', return_value=result):
            with self.assertRaises(k.PatchError):
                k.measure_rom(self.root)

    def test_generic_pokemon_source_entry_points_supported(self):
        (self.root / 'src/battle_test.c').rename(self.root / 'src/pokemon.c')
        _, after, _ = self.plan()
        self.assertIn('src/pokemon.c', after)
        self.assertNotIn('src/event_object_movement.c', after)
    def test_protected_overworld_entrypoint_refused(self):
        src = self.root / 'src/battle_test.c'
        self.put('src/event_object_movement.c', src.read_bytes())
        src.unlink()
        with self.assertRaisesRegex(k.PatchError, 'protected overworld'):
            self.plan()
    def test_partial_apply_interrupt_restores_all_files(self):
        saved = self.snapshot()
        before, after, _ = self.plan()
        out = self.output()
        original_writer = k.atomic_write
        calls = [0]
        def interrupted_writer(path, data):
            calls[0] += 1
            original_writer(path, data)
            if calls[0] == 2:
                raise KeyboardInterrupt
        with mock.patch.object(k, 'atomic_write', side_effect=interrupted_writer):
            with self.assertRaises(KeyboardInterrupt):
                k.apply_changes(self.root, before, after, out)
        self.assertEqual(saved, self.snapshot())

if __name__ == '__main__':
    unittest.main(verbosity=2)
