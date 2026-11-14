// Original Kyoto tree artwork: exact four user-approved reference-motion poses.
// No encounter rules, saved metatiles, collision data, flags or weather are changed.
#ifdef TREE_SWAY_ROM_TEST
#include "tree_sway_rom_bridge.h"
#else
#include "global.h"
#include "sprite.h"
#include "field_camera.h"
#include "fieldmap.h"
#include "event_object_movement.h"
#include "tree_encounter_sway.h"
#include "config/tree_encounter.h"
#include "tilesets.h"
#endif

#define TREE_SWAY_TAG 0x7C72
#define TREE_SWAY_COOKIE 0x5357
#define TREE_SWAY_CYCLE_TICKS 43
#define TREE_SWAY_FRAMES 4
#define TREE_SWAY_BYTES 2048

#include "data/tree_encounter_sway_gfx.h"

static void CanopyCallback(struct Sprite *sprite) { (void)sprite; }
static const struct OamData sCanopyOam = {
    .shape = SPRITE_SHAPE(64x32), .size = SPRITE_SIZE(64x32), .priority = 1,
};
static const struct SpriteTemplate sCanopyTemplate = {
    .tileTag = TREE_SWAY_TAG, .paletteTag = TREE_ENCOUNTER_LEAF_TAG,
    .oam = &sCanopyOam, .anims = gDummySpriteAnimTable, .images = NULL,
    .affineAnims = gDummySpriteAffineAnimTable, .callback = CanopyCallback,
};
static const struct SpriteSheet sCanopySheets[] = {
    {sTreeSway0[0], TREE_SWAY_BYTES, TREE_SWAY_TAG},
    {sTreeSway1[0], TREE_SWAY_BYTES, TREE_SWAY_TAG},
};
static const u16 sCanopyMetatiles[2][6] = {
    {3,4,11,12,19,20}, {5,6,13,14,21,22},
};

static struct Sprite *FindCanopy(u32 half)
{
    u32 i;
    for (i = 0; i < MAX_SPRITES; i++)
        if (gSprites[i].inUse && gSprites[i].callback == CanopyCallback
         && gSprites[i].data[7] == TREE_SWAY_COOKIE && gSprites[i].data[6] == (s16)half)
            return &gSprites[i];
    return NULL;
}
static const struct MapLayout *CanopyLayout(const struct Sprite *sprite)
{
    return (const struct MapLayout *)((u32)(u16)sprite->data[3] | ((u32)(u16)sprite->data[4] << 16));
}
static bool32 MatchesCanopy(s16 x, s16 y, u32 shape)
{
    u32 i;
    if (shape >= ARRAY_COUNT(sCanopyMetatiles)) return FALSE;
    for (i = 0; i < 6; i++)
        if (MapGridGetMetatileIdAt(x + (i & 1), y - 2 + (i >> 1)) != sCanopyMetatiles[shape][i])
            return FALSE;
    return TRUE;
}
void TreeEncounterSway_Release(void)
{
    struct Sprite *sprite = FindCanopy(0);
    struct Sprite *lower = FindCanopy(1);
    u32 i;
    if (sprite == NULL) sprite = lower;
    if (sprite != NULL && CanopyLayout(sprite) == gMapHeader.mapLayout)
        for (i = 0; i < 6; i++)
            CurrentMapDrawMetatileAt(sprite->data[0] + (i & 1), sprite->data[1] - 2 + (i >> 1));
    sprite = FindCanopy(0);
    if (sprite != NULL) DestroySprite(sprite);
    if (lower != NULL) DestroySprite(lower);
    if (GetSpriteTileStartByTag(TREE_SWAY_TAG) != 0xFFFF) FreeSpriteTilesByTag(TREE_SWAY_TAG);
}
static void DrawCanopyGround(s16 x, s16 y, u32 shape)
{
    u32 i,j;
    u16 ground[8];
    for (i = 0; i < 6; i++)
    {
        const u16 *original = gTileset_General.metatiles + sCanopyMetatiles[shape][i] * NUM_TILES_PER_METATILE;
        // Top/middle: ground is authored in layer 1, foliage in layer 2.
        // Bottom: ground is layer 0, the stationary trunk/shadow is layer 1.
        // Both original visual layers are reproduced in the two sprite halves.
        for (j = 0; j < 4; j++) { ground[j] = original[(i < 4 ? 4 : 0) + j]; ground[4+j] = 0; }
        DrawDoorMetatileAt(x + (i & 1), y - 2 + (i >> 1), ground);
    }
}
void TreeEncounterSway_Update(s16 x, s16 y, u8 shape, u16 phase)
{
    struct Sprite *upper, *lower, *sprite;
    u32 half, layout, frame, tick;
    u16 tileStart;
    u8 id, palette;
    s16 sx,sy;
    (void)phase; // Leaf timer is independent: its 336-tick wrap must not restart the sway.
    if (gMapHeader.mapLayout == NULL || !MatchesCanopy(x,y,shape)) { TreeEncounterSway_Release(); return; }
    palette = IndexOfSpritePaletteTag(TREE_ENCOUNTER_LEAF_TAG);
    if (palette == 0xFF) { TreeEncounterSway_Release(); return; }
    upper = FindCanopy(0); lower = FindCanopy(1);
    if (upper == NULL || lower == NULL || upper->data[0] != x || upper->data[1] != y
     || upper->data[2] != shape || CanopyLayout(upper) != gMapHeader.mapLayout
     || GetSpriteTileStartByTag(TREE_SWAY_TAG) == 0xFFFF)
    {
        TreeEncounterSway_Release();
        // Check the registered tag, not a zero return from older sheet loaders.
        LoadSpriteSheet(&sCanopySheets[shape]);
        tileStart = GetSpriteTileStartByTag(TREE_SWAY_TAG);
        if (tileStart == 0xFFFF) return;
        for (half = 0; half < 2; half++)
        {
            id = CreateSpriteAtEnd(&sCanopyTemplate, 0, 0, half ? 255 : 1);
            if (id >= MAX_SPRITES) { TreeEncounterSway_Release(); return; }
            sprite = &gSprites[id];
            sprite->data[0] = x; sprite->data[1] = y; sprite->data[2] = shape;
            layout = (u32)gMapHeader.mapLayout;
            sprite->data[3] = layout & 0xFFFF; sprite->data[4] = layout >> 16;
            sprite->data[5] = half ? 0 : -1; sprite->data[6] = half; sprite->data[7] = TREE_SWAY_COOKIE;
            sprite->coordOffsetEnabled = TRUE; sprite->animPaused = TRUE;
            sprite->oam.priority = half ? 2 : 1;
        }
        upper = FindCanopy(0); lower = FindCanopy(1);
        if (upper == NULL || lower == NULL) { TreeEncounterSway_Release(); return; }
    }
    tileStart = GetSpriteTileStartByTag(TREE_SWAY_TAG);
    // Four 180ms GIF poses represented by 11/11/10/11 video frames.
    // A full GBA cycle is 43 frames (~719.936ms, reference: 720ms).
    // Store the independent tick in the lower sprite; no persistent RAM is needed.
    tick = (u16)lower->data[5];
    if (tick >= TREE_SWAY_CYCLE_TICKS) tick = 0;
    frame = tick < 11 ? 0 : tick < 22 ? 1 : tick < 32 ? 2 : 3;
    lower->data[5] = tick + 1 == TREE_SWAY_CYCLE_TICKS ? 0 : tick + 1;
    // One 2KB VBlank copy updates BOTH halves atomically. Only one frame is
    // resident: no 32KB animation sheet and no writes during active scanout.
    if (upper->data[5] != (s16)frame)
    {
        const u8 *src = (const u8 *)(shape ? sTreeSway1[frame] : sTreeSway0[frame]);
        RequestSpriteCopy(src, (u8 *)OBJ_VRAM0 + tileStart * 32, TREE_SWAY_BYTES);
        upper->data[5] = frame;
    }
    SetSpritePosToMapCoords(x,y,&sx,&sy);
    for (half = 0; half < 2; half++)
    {
        sprite = half ? lower : upper;
        sprite->x = sx + 16; sprite->y = sy - 16 + half * 32;
        sprite->x2 = 0; sprite->y2 = 0;
        sprite->oam.tileNum = tileStart + half * 32; sprite->oam.paletteNum = palette;
        sprite->invisible = FALSE;
    }
    DrawCanopyGround(x,y,shape);
}
