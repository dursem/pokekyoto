#!/usr/bin/env python3
"""Tests for the tile id numbering, the capacity validator and the eight-word metatile migration."""

import os
import sys
import unittest

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import metatile_format as mf
import tilesetcap as tc


class VirtualTileTests(unittest.TestCase):
    def test_every_virtual_id_names_one_sheet_tile(self):
        seen = set()
        for tile in range(3072):
            secondary, local = tc.virtual_tile_to_local(tile)
            self.assertLess(local, 2048 if secondary else 1024)
            seen.add((secondary, local))
            word, ext = tile & 0x3FF, (tile >> 10) << tc.EXT_TILE_HIGH_SHIFT
            self.assertEqual(ext & ~(tc.EXT_TILE_HIGH_MASK << tc.EXT_TILE_HIGH_SHIFT), 0)
            self.assertEqual(word | (((ext >> tc.EXT_TILE_HIGH_SHIFT) & tc.EXT_TILE_HIGH_MASK) << 10), tile)
        self.assertEqual(len(seen), 3072)

    def test_boundaries(self):
        cases = {511: (False, 511), 1024: (False, 512), 1535: (False, 1023),
                 1023: (True, 511), 1536: (True, 512), 2047: (True, 1023), 2048: (True, 1024), 3071: (True, 2047)}
        for tile, expected in cases.items():
            self.assertEqual(tc.virtual_tile_to_local(tile), expected)

    def test_tile_words_keep_flip_and_palette_bits(self):
        data = tc.TilesetData.__new__(tc.TilesetData)
        data.metatiles = [0xFC00 | 0x3FF] * 12
        data.ext = bytes([3 << tc.EXT_TILE_HIGH_SHIFT] * 12)
        self.assertEqual(data.virtual_tiles(0), [0xFFF] * 12)


class MigrationTests(unittest.TestCase):
    class Sheets:
        """Every tile has transparent pixels except tile 7, which is solid."""
        anim = frozenset({9})

        def tile(self, virtual):
            pixels = np.zeros((8, 8), np.uint8)
            pixels[:, :4] = 1
            if virtual == 7:
                pixels[:] = 1
            return pixels

    def place(self, layer, words):
        return mf.old_placement(layer, words, [0] * 8, None, [0] * 4, None)

    def test_layer_types_land_on_their_backgrounds(self):
        words = list(range(1, 9))
        normal = self.place(mf.NORMAL, words)
        covered = self.place(mf.COVERED, words)
        split = self.place(mf.SPLIT, words)
        for i in range(4):
            self.assertEqual((normal[i][1][0], normal[i][2][0]), (1 + i, 5 + i))
            self.assertIsNone(normal[i][0])
            self.assertEqual((covered[i][0][0], covered[i][1][0], covered[i][2][0]), (1 + i, 5 + i, 0))
            self.assertEqual((split[i][0][0], split[i][1][0], split[i][2][0]), (1 + i, 0, 5 + i))

    def test_triple_uses_the_third_layer(self):
        cells = mf.old_placement(mf.TRIPLE, list(range(1, 9)), [0] * 8, [20, 21, 22, 23], [0x20] * 4, None)
        self.assertEqual([c[2][:2] for c in cells], [(20, 0x20), (21, 0x20), (22, 0x20), (23, 0x20)])

    def test_filler_kept_only_where_it_shows(self):
        sheets = [self.Sheets()]
        self.assertTrue(mf.filler_needed((1, 0, 0), (2, 0, 0), sheets))       # both half transparent
        self.assertFalse(mf.filler_needed((1 | mf.HFLIP, 0, 0), (2, 0, 0), sheets))  # mirrored halves cover
        self.assertFalse(mf.filler_needed((7, 0, 0), (2, 0, 0), sheets))     # solid middle
        self.assertTrue(mf.filler_needed((7, 0, 0), (9, 0, 0), sheets))      # animated top

    def test_unknown_partner_keeps_the_filler(self):
        class Unknown(self.Sheets):
            def tile(self, virtual):
                return None
        self.assertTrue(mf.filler_needed((7, 0, 0), (7, 0, 0), [Unknown()]))

    def test_palette_rows_follow_the_words(self):
        refs = [3, 3, 3, 3, 16, 16, 16, 16]
        cells = mf.old_placement(mf.SPLIT, [0x3001] * 4 + [0x0002] * 4, [0] * 8, None, [0] * 4, refs)
        self.assertEqual([c[0][2] for c in cells], [3] * 4)
        self.assertEqual([c[1][2] for c in cells], [0] * 4)
        self.assertEqual([c[2][2] for c in cells], [16] * 4)


if __name__ == '__main__':
    unittest.main()
