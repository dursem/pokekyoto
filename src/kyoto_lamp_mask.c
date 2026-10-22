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

// These inputs describe the pending field frame. Nothing queues BG tiles between
// BeginFrame and the last lamp; never retain graphics pointers across frames.
#define TILE_LOOKUP_COUNT 64
static EWRAM_DATA struct
{
    const u16 *map;
    u16 priority, tileBase;
} sMaskBgs[2] = {0};
static EWRAM_DATA struct
{
    const u8 *pixels;
    u16 tilePlusOne;
} sTileLookups[TILE_LOOKUP_COUNT] = {0};
static s16 sMaskScrollX, sMaskScrollY;

void KyotoLamp_BeginFrame(void)
{
    GetCameraOffsetWithPan(&sMaskScrollX, &sMaskScrollY);
    for (u32 i = 0; i < ARRAY_COUNT(sMaskBgs); i++)
    {
        u32 bg = i + 1;
        u32 cnt = GetGpuReg(REG_OFFSET_BG0CNT + bg * 2);
        sMaskBgs[i].map = GetBgTilemapBuffer(bg);
        sMaskBgs[i].priority = cnt & 3;
        sMaskBgs[i].tileBase = ((cnt >> 2) & 3) * 512;
    }
    memset(sTileLookups, 0, sizeof(sTileLookups));
}

static const u8 *TileGraphics(u32 tile)
{
    u32 slot = (tile ^ (tile >> 6)) & (TILE_LOOKUP_COUNT - 1);
    if (sTileLookups[slot].tilePlusOne != tile + 1)
    {
        sTileLookups[slot].pixels = MapPalettesGetTileGraphics(tile);
        sTileLookups[slot].tilePlusOne = tile + 1;
    }
    return sTileLookups[slot].pixels;
}

static ARM_FUNC __attribute__((section(".iwram.code"), noinline)) bool32 BehindMap(u32 x, u32 y, u32 layers)
{
    x = (x + sMaskScrollX) & 255;
    y = (y + sMaskScrollY) & 255;
    for (u32 bg = 0; bg < ARRAY_COUNT(sMaskBgs); bg++)
    {
        const u16 *map = sMaskBgs[bg].map;
        u32 entry, tx = x & 7, ty = y & 7;
        if (!(layers & (1 << bg)))
            continue;
        entry = map[(y / 8) * 32 + x / 8];
        if (entry & 0x400) tx ^= 7;
        if (entry & 0x800) ty ^= 7;
        if (Pixel4(TileGraphics((entry & 1023) + sMaskBgs[bg].tileBase), 8, tx, ty))
            return TRUE;
    }
    return FALSE;
}

struct MaskOverlap
{
    u8 *pixels;
    const u8 *art;
    s32 width, height, ax, ay, lx, ly, x0, y0, x1, y1;
    u32 flips, layers;
};

// Measured hot path: ARM instructions in fast RAM reduce both fetch cost and
// register spills. The linker retains the existing 3 KiB stack-headroom check.
static ARM_FUNC __attribute__((section(".iwram.code"), noinline)) bool32 CutActor(const struct MaskOverlap *overlap)
{
    u8 *pixels = overlap->pixels;
    const u8 *art = overlap->art;
    s32 width = overlap->width, height = overlap->height;
    s32 ax = overlap->ax, ay = overlap->ay, lx = overlap->lx, ly = overlap->ly;
    s32 x0 = overlap->x0, y0 = overlap->y0, x1 = overlap->x1, y1 = overlap->y1;
    u32 flips = overlap->flips, layers = overlap->layers;
    bool32 cut = FALSE;
    // Tile row bases and vertical flips are constant for the entire row.
    // Keeping them out of the pixel loop also limits ARM7 register spills.
    for (s32 y = y0; y < y1; y++)
    {
        u32 sy = y - ay, dy = y - ly;
        u32 lampRow = (dy / 8) * 128 + (dy & 7) * 4;
        u32 actorRow;
        if (flips & 16) sy = height - 1 - sy;
        actorRow = (sy / 8) * (width / 8) * 32 + (sy & 7) * 4;
        for (s32 x = x0; x < x1; x++)
        {
            u32 dx = x - lx, sx = x - ax;
            u32 offset = lampRow + (dx / 8) * 32 + (dx & 7) / 2;
            u32 shift = (dx & 1) * 4;
            u32 actorOffset;
            // Empty glow pixels (including earlier actors' cutouts) cannot
            // change the mask. Avoid sprite and map sampling for them.
            if (!((pixels[offset] >> shift) & 15))
                continue;
            if (flips & 8) sx = width - 1 - sx;
            actorOffset = actorRow + (sx / 8) * 32 + (sx & 7) / 2;
            if (!((art[actorOffset] >> ((sx & 1) * 4)) & 15)
             || (layers && BehindMap(x, y, layers)))
                continue;
            pixels[offset] &= ~(15 << shift);
            cut = TRUE;
        }
    }
    return cut;
}

void KyotoLamp_Prepare(const struct Sprite *sprite, u32 index)
{
    struct OamData *lamp = NULL;
    s32 lx = sprite->oam.x, ly = sprite->oam.y;
    u32 work[MASK_BYTES / 4];
    bool32 cut = FALSE;
    u8 *pixels = (u8 *)work;
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
    for (u32 i = 0; i < 128; i++)
    {
        const struct OamData *actor = &gMain.oamBuffer[i];
        s32 ax = actor->x, ay = actor->y, width, height;
        s32 x0, y0, x1, y1;
        const u8 *art;
        u32 dim, layers = 0;
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
        for (u32 bg = 0; bg < ARRAY_COUNT(sMaskBgs); bg++)
            if (sMaskBgs[bg].map && sMaskBgs[bg].priority < actor->priority)
                layers |= 1 << bg;
        art = GetSpriteGraphicsForPendingFrame((const u8 *)(OBJ_VRAM0 + actor->tileNum * 32), width * height / 2);
        {
            struct MaskOverlap overlap = {pixels, art, width, height, ax, ay, lx, ly, x0, y0, x1, y1, actor->matrixNum, layers};
            if (CutActor(&overlap))
                cut = TRUE;
        }
    }
    if (cut)
    {
        bool32 allocated = FALSE;
        u32 tile = GetSpriteTileStartByTag(MASK_TAG_BASE + index);
        if (tile == 0xFFFF)
        {
            struct SpriteSheet sheet = {sLampPixels[index], MASK_BYTES, MASK_TAG_BASE + index};
            if (!CanAllocSpriteTiles(MASK_BYTES / 32))
            {
                lamp->affineMode = ST_OAM_AFFINE_OFF;
                lamp->y = DISPLAY_HEIGHT; // Hide only this glow if OBJ tiles are exhausted.
                return;
            }
            CpuCopy32(pixels, sLampPixels[index], MASK_BYTES);
            LoadSpriteSheet(&sheet);
            allocated = TRUE;
            tile = GetSpriteTileStartByTag(sheet.tag);
            if (tile == 0xFFFF)
            {
                lamp->affineMode = ST_OAM_AFFINE_OFF;
                lamp->y = DISPLAY_HEIGHT;
                return;
            }
        }
        // Keep the queued bytes intact until the admitted VBlank consumes them.
        // dirty must survive an unchanged rebuild, including a blocked VBlank.
        if (allocated || sMasks[index].tilePlusOne != tile + 1
         || memcmp(sLampPixels[index], pixels, MASK_BYTES) != 0)
        {
            CpuCopy32(pixels, sLampPixels[index], MASK_BYTES);
            sMasks[index].dirty = TRUE;
        }
        sMasks[index].tilePlusOne = tile + 1;
        sMasks[index].originalTile = sprite->oam.tileNum;
        lamp->tileNum = tile;
    }
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
