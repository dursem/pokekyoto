#!/usr/bin/env python3
# Codec sanity test for the Opal logical numbering used by the Porymap patch.
P=512
PE=1024
TOTAL=3072

def game_to_editor(tile):
    if tile < 512: return tile
    if tile < 1024: return 1024 + tile - 512
    if tile < 1536: return tile - 512
    return tile

def editor_to_game(tile):
    if tile < 512: return tile
    if tile < 1024: return tile + 512
    if tile < 1536: return tile - 1024 + 512
    return tile

for e in range(TOTAL):
    g=editor_to_game(e)
    assert 0 <= g < TOTAL
    assert game_to_editor(g)==e, (e,g,game_to_editor(g))
for g in range(TOTAL):
    e=game_to_editor(g)
    assert 0 <= e < TOTAL
    assert editor_to_game(e)==g, (g,e,editor_to_game(e))
print("PASS: all 3072 logical tile IDs round-trip")
