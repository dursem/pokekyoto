"""Regression checks for logical lavender tiles and static tile deduplication."""
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import patch

import numpy as np
from PIL import Image
import tilesetcap as tc


class StreamedAnimationTests(unittest.TestCase):
    def sheet(self):
        return SimpleNamespace(symbol='gTileset_Petalburg', isSecondary=True,
                               numTiles=2048, dir='data/tilesets/secondary/petalburg')

    def test_logical_tiles_are_not_physical_pins(self):
        protected = tc.streamed_animation_tiles({'gTileset_Petalburg': self.sheet()})
        self.assertEqual(protected, {'gTileset_Petalburg': {2304, 2305, 2320, 2321} | {1024 + y * 16 + x for y in range(124, 128) for x in range(12, 14)}})
        self.assertEqual(tc.animation_slots_by_init()['InitTilesetAnim_Petalburg'], set())

    def test_malformed_or_out_of_bounds_animation_is_rejected(self):
        sheet = self.sheet()
        sheet.numTiles = 1280
        with self.assertRaises(ValueError):
            tc.streamed_animation_tiles({'gTileset_Petalburg': sheet})
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp, 'src')
            source.mkdir()
            source.joinpath('tileset_anims.c').write_text(
                'sStreamedAnimTiles[] = {\n{ &gTileset_Petalburg, BAD_TILE, 0 },\n};\n')
            with patch.object(tc, 'ROOT', tmp), self.assertRaises(ValueError):
                tc.streamed_animation_tiles({'gTileset_Petalburg': self.sheet()})

    def test_duplicate_graphics_never_alias_to_or_from_animation(self):
        # A static duplicate after the animated tile must not inherit its motion.
        # An animated tile equal to an earlier static tile must also stay independent.
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp, 'sheet')
            folder.mkdir()
            pixels = np.zeros((8, 32), dtype=np.uint8)
            pixels[:, :24] = 3
            pixels[:, 24:] = 4
            im = Image.fromarray(pixels).convert('P')
            im.putpalette([c for i in range(16) for c in (i * 16,) * 3])
            im.save(folder / 'tiles.png', bits=4)
            sheet = SimpleNamespace(symbol='gTileset_Petalburg', isSecondary=True,
                                    numTiles=4, dir=str(folder))
            aliases = tc.static_aliases(sheet, {513})
            self.assertNotIn(513, aliases)
            self.assertNotIn(513, aliases.values())
            self.assertEqual(aliases[514], 512)

    def test_frame_zero_and_all_four_palette_indices(self):
        folder = Path(tc.ROOT, 'graphics/tileset_anims/petalburg/lavender')
        base = Image.open(tc.path('data/tilesets/secondary/petalburg/tiles.png'))
        frames = [Image.open(folder / f'{i}.png') for i in range(4)]
        self.assertTrue(np.array_equal(np.asarray(frames[0]), np.asarray(base)[640:656, :16]))
        pal = Path(tc.path('data/tilesets/secondary/petalburg/palettes/12.pal'))
        colors = [int(c) for line in pal.read_text().splitlines()[3:19] for c in line.split()]
        for frame in frames:
            self.assertEqual(frame.size, (16, 16))
            self.assertEqual(frame.mode, 'P')
            self.assertEqual(frame.getpalette()[:48], colors)
            self.assertLessEqual(int(np.asarray(frame).max()), 15)
        self.assertEqual(len({frame.tobytes() for frame in frames}), 4)

    def test_large_metatiles_reconstruct_the_reference(self):
        folder = Path(tc.ROOT, 'data/tilesets/secondary/petalburg')
        sheet = np.array(Image.open(folder / 'tiles.png'))
        words = np.fromfile(folder / 'metatiles.bin', dtype='<u2').reshape(-1, 12)
        ext = np.fromfile(folder / 'metatile_tiles_ext.bin', dtype='u1').reshape(-1, 12)
        result = np.zeros((32, 16), dtype=np.uint8)
        for m, (mx, my) in zip(range(1371, 1373), [(0, 0), (0, 16)]):
            for q, (x, y) in enumerate([(0, 0), (8, 0), (0, 8), (8, 8)]):
                word, high = int(words[m, 4 + q]), int(ext[m, 4 + q])
                virtual = (word & 1023) | (((high >> 5) & 3) << 10)
                local = virtual - 1024
                self.assertEqual(word >> 12, 12)
                self.assertEqual(word & 0xC00, 0)
                result[my+y:my+y+8, mx+x:mx+x+8] = sheet[(local//16)*8:(local//16)*8+8, (local%16)*8:(local%16)*8+8]
        frames = [Image.open(Path(tc.ROOT, 'graphics/tileset_anims/petalburg/lavender_large', f'{i}.png')) for i in range(4)]
        self.assertTrue(np.array_equal(result, np.array(frames[0])))
        palette = [int(c) for line in (folder / 'palettes/12.pal').read_text().splitlines()[3:19] for c in line.split()]
        for frame in frames:
            self.assertEqual(frame.size, (16, 32))
            self.assertEqual(frame.getpalette()[:48], palette)
            self.assertTrue(set(np.unique(np.array(frame))).issubset({0, 1, 2, 5, 6, 12, 13, 14}))


if __name__ == '__main__':
    unittest.main()
