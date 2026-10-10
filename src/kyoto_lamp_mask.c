// A priority-1 glow illuminates lamp caps on BG1. Transparent cutouts preserve
// the visible pixels of actors underneath it, without changing actor priority.
#include "global.h"
#include "bg.h"
#include "field_camera.h"
#include "gpu_regs.h"
#include "main.h"
#include "map_palette.h"
#include "sprite.h"
#include "kyoto_lamp_mask.h"

#define MASK_COUNT 16
#define MASK_TAG_BASE 0xD9E0
#define MASK_BYTES (32 * 32 / 2)

extern const u32 gFieldEffectObjectPic_BallLight[];
static EWRAM_DATA u32 sLampPixels[MASK_COUNT][MASK_BYTES / 4] = {0};
static EWRAM_DATA struct { u16 tilePlusOne, originalTile; bool8 dirty; } sMasks[MASK_COUNT] = {0};
static const u8 sWidths[] = {8,16,32,64,16,32,32,64,8,8,16,32};
static const u8 sHeights[] = {8,16,32,64,8,8,16,32,16,32,32,64};

static void ReleaseMask(u32 index)
{
    u32 tile;
    if (!sMasks[index].tilePlusOne)
        return;
    tile = GetSpriteTileStartByTag(MASK_TAG_BASE + index);
    if (tile == sMasks[index].tilePlusOne - 1)
    {
        for (u32 i = 0; i < 128; i++)
            if (gMain.oamBuffer[i].tileNum == tile)
                gMain.oamBuffer[i].tileNum = sMasks[index].originalTile;
        FreeSpriteTilesByTag(MASK_TAG_BASE + index);
    }
    memset(&sMasks[index], 0, sizeof(sMasks[index]));
}

void KyotoLamp_Finish(u32 count)
{
    for (u32 i = count; i < MASK_COUNT; i++)
        ReleaseMask(i);
}

static u32 Pixel4(const u8 *pixels, u32 width, u32 x, u32 y)
{
    u32 offset = ((y / 8) * (width / 8) + x / 8) * 32 + (y & 7) * 4 + (x & 7) / 2;
    return (pixels[offset] >> ((x & 1) * 4)) & 15;
}

static bool32 BehindMap(u32 x, u32 y, u32 priority, s16 scrollX, s16 scrollY)
{
    x = (x + scrollX) & 255;
    y = (y + scrollY) & 255;
    for (u32 bg = 1; bg < 3; bg++)
    {
        u32 cnt = GetGpuReg(REG_OFFSET_BG0CNT + bg * 2);
        const u16 *map = GetBgTilemapBuffer(bg);
        u32 entry, tx = x & 7, ty = y & 7;
        if (!map || (cnt & 3) >= priority)
            continue;
        entry = map[(y / 8) * 32 + x / 8];
        if (entry & 0x400) tx ^= 7;
        if (entry & 0x800) ty ^= 7;
        if (Pixel4(MapPalettesGetTileGraphics((entry & 1023) + ((cnt >> 2) & 3) * 512), 8, tx, ty))
            return TRUE;
    }
    return FALSE;
}

void KyotoLamp_Prepare(const struct Sprite *sprite, u32 index)
{
    struct OamData *lamp = NULL;
    s32 lx = sprite->oam.x, ly = sprite->oam.y;
    s16 scrollX, scrollY;
    bool32 cut = FALSE;
    u8 *pixels = (u8 *)sLampPixels[index];
    if (lx >= 256) lx -= 512;
    if (ly >= 160) ly -= 256;
    for (u32 i = 0; i < 128; i++)
    {
        struct OamData *oam = &gMain.oamBuffer[i];
        if (oam->objMode == ST_OAM_OBJ_BLEND && oam->affineMode == ST_OAM_AFFINE_NORMAL
         && oam->tileNum == sprite->oam.tileNum && oam->paletteNum == sprite->oam.paletteNum
         && oam->x == sprite->oam.x && oam->y == sprite->oam.y)
        {
            lamp = oam;
            break;
        }
    }
    if (!lamp || lx >= 240 || ly >= 160 || lx + 32 <= 0 || ly + 32 <= 0)
    {
        ReleaseMask(index);
        return;
    }
    CpuCopy32(gFieldEffectObjectPic_BallLight, pixels, MASK_BYTES);
    GetCameraOffsetWithPan(&scrollX, &scrollY);
    for (u32 i = 0; i < 128; i++)
    {
        const struct OamData *actor = &gMain.oamBuffer[i];
        s32 ax = actor->x, ay = actor->y, width, height;
        s32 x0, y0, x1, y1;
        const u8 *art;
        u32 dim;
        if (actor->objMode != ST_OAM_OBJ_NORMAL || actor->affineMode != ST_OAM_AFFINE_OFF
         || actor->bpp != ST_OAM_4BPP || actor->shape == 3 || actor->y == 160 || actor->priority < 1)
            continue;
        dim = actor->shape * 4 + actor->size;
        width = sWidths[dim]; height = sHeights[dim];
        if (ax >= 256) ax -= 512;
        if (ay >= 160) ay -= 256;
        x0 = max(max(ax, lx), 0); y0 = max(max(ay, ly), 0);
        x1 = min(min(ax + width, lx + 32), 240); y1 = min(min(ay + height, ly + 32), 160);
        if (x0 >= x1 || y0 >= y1) continue;
        art = GetSpriteGraphicsForPendingFrame((const u8 *)(OBJ_VRAM0 + actor->tileNum * 32), width * height / 2);
        for (s32 y = y0; y < y1; y++)
            for (s32 x = x0; x < x1; x++)
            {
                u32 sx = x - ax, sy = y - ay, dx = x - lx, dy = y - ly;
                u32 offset, shift;
                if (actor->matrixNum & 8) sx = width - 1 - sx;
                if (actor->matrixNum & 16) sy = height - 1 - sy;
                if (!Pixel4(art, width, sx, sy) || BehindMap(x, y, actor->priority, scrollX, scrollY))
                    continue;
                offset = ((dy / 8) * 4 + dx / 8) * 32 + (dy & 7) * 4 + (dx & 7) / 2;
                shift = (dx & 1) * 4;
                if ((pixels[offset] >> shift) & 15)
                {
                    pixels[offset] &= ~(15 << shift);
                    cut = TRUE;
                }
            }
    }
    if (cut)
    {
        u32 tile = GetSpriteTileStartByTag(MASK_TAG_BASE + index);
        if (tile == 0xFFFF)
        {
            struct SpriteSheet sheet = {pixels, MASK_BYTES, MASK_TAG_BASE + index};
            if (!CanAllocSpriteTiles(MASK_BYTES / 32))
            {
                lamp->affineMode = ST_OAM_AFFINE_OFF;
                lamp->y = DISPLAY_HEIGHT; // Hide only this glow if OBJ tiles are exhausted.
                return;
            }
            LoadSpriteSheet(&sheet);
            tile = GetSpriteTileStartByTag(sheet.tag);
            if (tile == 0xFFFF)
            {
                lamp->affineMode = ST_OAM_AFFINE_OFF;
                lamp->y = DISPLAY_HEIGHT;
                return;
            }
        }
        sMasks[index].tilePlusOne = tile + 1;
        sMasks[index].originalTile = sprite->oam.tileNum;
        sMasks[index].dirty = TRUE;
        lamp->tileNum = tile;
    }
    else
        sMasks[index].dirty = FALSE; // Cache the private tiles until field cleanup.
    // Ball lights are fixed 32x32 art; render without a shared affine matrix.
    lamp->affineMode = ST_OAM_AFFINE_OFF;
    lamp->matrixNum = 0;
    lamp->priority = 1;
}

void KyotoLamp_VBlank(void)
{
    for (u32 i = 0; i < MASK_COUNT; i++)
        if (sMasks[i].dirty && sMasks[i].tilePlusOne)
        {
            DmaCopy16(3, sLampPixels[i], (void *)(OBJ_VRAM0 + (sMasks[i].tilePlusOne - 1) * 32), MASK_BYTES);
            sMasks[i].dirty = FALSE;
        }
}
