# Petalburg lavender, 16x24 native artwork (V5)

The generated reference has uneven block widths and heights. Its visible grid
is reconstructed to 16x24 native pixels; no geometric scaling is used. These
frames are padded with eight transparent rows on TOP to make 16x32 engine
assets. This is a grid reconstruction, not a pixel-perfect copy of the JPEG.
The original palette 12 is unchanged. Four poses, 24 updates each (~1.6s loop).

Place global 0x7FB (secondary 1371) above 0x7FC (secondary 1372). This is one
metatile wide and two high. Source pixels are on the middle layer over the
original flower's grass. Set map collision and elevation in the editor.

Only tilesheet x96..111, y992..1023 is used. Local 8x8 IDs are 1996,1997,
2012,2013,2028,2029,2044,2045. Frame-tile IDs 4..11 select these eight tiles in
the large-frame bank. The four legacy 16x16 tiles keep their existing bank and
12-update timing. Streaming, cache ownership and dedup protection are retained.

V3/V4 used four metatiles in a 2x2 block. V5 restores slots 0x7FD/0x7FE and the
unused right half of that raw tilesheet block to their original grass/blank
values. No maps are changed. If the older 2x2 version was placed, replace that
placement with the new vertical pair in the editor.

Frames: graphics/tileset_anims/petalburg/lavender_large/0.png through 3.png.
The visible 16x24 native frame is the bottom 24 rows of each 16x32 file.

Validation: 13 Python tests; host C cache/animation harness with ASan/UBSan;
tileset-capacity check; full GBA build. Metatile tests reconstruct the full
16x32 canvas. Runtime checks cover both banks, all eight new tiles, four poses,
palettes/flips, eviction, unrelated slots and layout reloads. No mGBA test.
