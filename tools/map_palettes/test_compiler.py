import copy
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import shutil
import tempfile
import unittest

from compiler import PaletteError, compile_project, connection_kind, enable, metatile_masks, validate_mask


class PaletteCompilerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.write('src/data/tilesets/headers.h', '''
const struct Tileset gPrimary = {.isSecondary = FALSE, .metatiles = gPrimaryMetatiles};
const struct Tileset gSecondary = {.isSecondary = TRUE, .metatiles = gSecondaryMetatiles};
''')
        self.write('src/data/tilesets/metatiles.h', '''
const u16 gPrimaryMetatiles[] = INCBIN_U16("data/tilesets/primary/metatiles.bin");
const u16 gSecondaryMetatiles[] = INCBIN_U16("data/tilesets/secondary/metatiles.bin");
''')
        self.bin('data/tilesets/primary/metatiles.bin', [0] * 12)
        self.bin('data/tilesets/secondary/metatiles.bin', [0x6001] * 12 * 18)
        self.refs = [[i] * 12 for i in range(18)]
        self.sidecar(self.refs)
        self.write('data/tilesets/secondary/palettes/06.pal', 'JASC-PAL\n0100\n16\n' + '248 0 0\n' * 16)
        self.layout = dict(id='LAYOUT_TEST', name='Test_Layout', width=48, height=2,
                           primary_tileset='gPrimary', secondary_tileset='gSecondary', layout_version='emerald',
                           blockdata_filepath='data/layouts/Test/map.bin', border_filepath='data/layouts/Test/border.bin')
        self.layouts = [self.layout]
        self.map = dict(id='MAP_TEST', name='Test', layout='LAYOUT_TEST', connections=[])
        self.maps = [self.map]
        self.bin(self.layout['blockdata_filepath'], [512] * 96)
        self.bin(self.layout['border_filepath'], [0] * 4)
        self.spec = dict(tileset='gSecondary', palette_refs='data/tilesets/secondary/palette_refs.json',
                         palettes=[{'day': 'data/tilesets/secondary/palettes/06.pal'} for _ in range(4)])
        self.manifest = dict(version=1, tilesets=[self.spec])

    def sidecar(self, references):
        metatiles = self.root / 'data/tilesets/secondary/metatiles.bin'
        self.json('data/tilesets/secondary/palette_refs.json', {
            'version': 1,
            'metatiles_sha256': hashlib.sha256(metatiles.read_bytes()).hexdigest(),
            'palettes': references,
        })

    def write(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def json(self, name, data):
        self.write(name, json.dumps(data))

    def bin(self, name, data):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(struct.pack(f'<{len(data)}H', *data))

    def compile(self, game='emerald'):
        self.json('data/layouts/layouts.json', {'layouts': self.layouts})
        for data in self.maps:
            self.json(f"data/maps/{data['name']}/map.json", data)
        return compile_project(self.root, self.manifest, game)

    def test_four_extras_generate_without_changing_metatile_words(self):
        self.bin(self.layout['blockdata_filepath'], [526, 527, 528, 529] * 24)
        original = (self.root / 'data/tilesets/secondary/metatiles.bin').read_bytes()
        source, report = self.compile()
        self.assertIn('sMapPaletteExtra_gSecondary[4][16]', source)
        self.assertEqual(report[0]['peak'], 5)
        self.assertEqual(original, (self.root / 'data/tilesets/secondary/metatiles.bin').read_bytes())

    def test_generated_data_compiles_with_gba_abi(self):
        compiler = shutil.which('arm-none-eabi-gcc')
        if compiler is None:
            compiler = Path('/opt/devkitpro/devkitARM/bin/arm-none-eabi-gcc')
        if not Path(compiler).exists():
            self.skipTest('devkitARM not installed')
        source, _ = self.compile()
        probe = self.root / 'probe.c'
        probe.write_text('#include "global.h"\n#include "map_palette.h"\n'
                         'extern const struct Tileset gSecondary;\n' + source)
        result = subprocess.run([str(compiler), '-fsyntax-only', '-mthumb', '-mabi=apcs-gnu',
                                 '-march=armv4t', '-std=gnu17', '-DMODERN=1', '-DEMERALD',
                                 '-iquote', str(Path(__file__).resolve().parents[2] / 'include'), str(probe)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_more_authored_palettes_than_simultaneous_banks(self):
        # Two separated regions, each using ten of an eighteen-palette library.
        row = [512 + i % 10 for i in range(14)] + [512] * 20 + [520 + i % 10 for i in range(14)]
        self.bin(self.layout['blockdata_filepath'], row * 2)
        self.assertLessEqual(self.compile()[1][0]['peak'], 14)

    def test_overflow_reports_camera_and_logical_ids(self):
        self.bin(self.layout['blockdata_filepath'], [512 + i % 18 for i in range(96)])
        with self.assertRaisesRegex(PaletteError, r'camera=.*exceed.*logical='):
            self.compile()

    def test_reservations_and_dynamic_mutations_count_together(self):
        self.spec['reserved_banks'] = [12, 13]
        self.spec['dynamic_metatiles'] = list(range(513, 526))
        with self.assertRaisesRegex(PaletteError, '14 palettes exceed 12'):
            self.compile()

    def test_direct_script_mutations_are_inferred(self):
        self.write('data/maps/Test/scripts.pory', 'script Foo { setmetatile(1, 1, 529, FALSE) }')
        self.assertEqual(self.compile()[1][0]['peak'], 2)

    def test_shop_includes_offscreen_library(self):
        self.write('data/maps/Test/scripts.pory', 'script Foo { pokemart(Items) }')
        with self.assertRaisesRegex(PaletteError, '18 palettes exceed 12'):
            self.compile()

    def test_sidecar_does_not_truncate_ids_16_and_17(self):
        values, rows = metatile_masks(self.root / 'data/tilesets/secondary/metatiles.bin', self.refs)
        self.assertEqual(values[17], 1 << 17)
        self.assertEqual(rows[16], [16] * 12)
        self.refs[0][0] = 18
        self.sidecar(self.refs)
        with self.assertRaises(PaletteError):
            self.compile()

    def test_malformed_sidecar_reports_a_diagnostic(self):
        self.sidecar(None)
        with self.assertRaisesRegex(PaletteError, 'palettes must be an array'):
            self.compile()

    def test_mismatched_binary_and_sidecar_are_rejected(self):
        self.bin('data/tilesets/secondary/metatiles.bin', [0x6002] * 12 * 18)
        with self.assertRaisesRegex(PaletteError, 'checksum mismatch'):
            self.compile()

    def test_wrong_sidecar_shape_rejected(self):
        self.sidecar([[1] * 8])
        with self.assertRaisesRegex(PaletteError, '18 rows'):
            self.compile()

    def test_pinned_reserved_conflict_rejected(self):
        with self.assertRaisesRegex(PaletteError, 'conflicts'):
            validate_mask(1, 1, 1, 'test')

    def test_one_way_incoming_connection_from_another_primary_rejected(self):
        other = copy.deepcopy(self.layout)
        other.update(id='LAYOUT_OTHER', name='Other_Layout', primary_tileset='gSecondary', secondary_tileset='gPrimary')
        self.layouts.append(other)
        self.maps.append(dict(id='MAP_OTHER', name='Other', layout='LAYOUT_OTHER',
                              connections=[dict(direction='right', offset=0, map='MAP_TEST')]))
        with self.assertRaisesRegex(PaletteError, r'\(incoming\).*same primary tileset'):
            self.compile()

    def test_one_way_incoming_connection_from_another_secondary_reloads(self):
        # Same primary, another library: the engine reloads every bank and redraws when crossing.
        other = copy.deepcopy(self.layout)
        other.update(id='LAYOUT_OTHER', name='Other_Layout', secondary_tileset='gPrimary')
        self.layouts.append(other)
        self.maps.append(dict(id='MAP_OTHER', name='Other', layout='LAYOUT_OTHER',
                              connections=[dict(direction='right', offset=0, map='MAP_TEST')]))
        self.compile()

    def test_connection_kind(self):
        a = dict(primary_tileset='gP', secondary_tileset='gA', layout_version='emerald')
        self.assertEqual(connection_kind(a, dict(a), 'x'), 'seamless')
        self.assertEqual(connection_kind(a, dict(a, secondary_tileset='gB'), 'x'), 'reload')
        with self.assertRaises(PaletteError):
            connection_kind(a, dict(a, primary_tileset='gQ'), 'x')
        with self.assertRaises(PaletteError):
            connection_kind(a, dict(a, layout_version='frlg'), 'x')

    def test_connected_neighbor_palettes_are_counted(self):
        other = copy.deepcopy(self.layout)
        other.update(id='LAYOUT_OTHER', name='Other_Layout', blockdata_filepath='data/layouts/Other/map.bin')
        self.layouts.append(other)
        self.bin(other['blockdata_filepath'], [512 + i % 18 for i in range(96)])
        self.maps.append(dict(id='MAP_OTHER', name='Other', layout='LAYOUT_OTHER', connections=[]))
        self.map['connections'] = [dict(direction='right', offset=0, map='MAP_OTHER')]
        with self.assertRaisesRegex(PaletteError, 'exceed'):
            self.compile()

    def test_frlg_primary_boundary(self):
        self.layout['layout_version'] = 'frlg'
        self.bin(self.layout['blockdata_filepath'], [640 + 17] * 96)
        self.assertEqual(self.compile('firered')[1][0]['peak'], 2)

    def test_unknown_declaration_rejected(self):
        self.spec['reserved_bank'] = [13]
        with self.assertRaisesRegex(PaletteError, 'Unknown tileset fields'):
            self.compile()

    def test_enable_preserves_legacy_binary_and_is_not_destructive(self):
        sidecar = self.root / self.spec['palette_refs']
        sidecar.unlink()
        self.json('data/tilesets/map_palettes.json', dict(version=1, tilesets=[]))
        original = (self.root / 'data/tilesets/secondary/metatiles.bin').read_bytes()
        enable(self.root, 'gSecondary')
        self.assertEqual(json.loads(sidecar.read_text())['palettes'], [[6] * 12] * 18)
        self.assertEqual(original, (self.root / 'data/tilesets/secondary/metatiles.bin').read_bytes())
        with self.assertRaisesRegex(PaletteError, 'already enabled'):
            enable(self.root, 'gSecondary')


if __name__ == '__main__':
    unittest.main()
