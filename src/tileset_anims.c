#include "global.h"
#include "fieldmap.h"
#include "tile_cache.h"
#include "tilesets.h"
#include "tileset_anims.h"

#define LAVENDER_FRAME_TICKS 12
#define LAVENDER_LARGE_FRAME_TICKS 24
#define LAVENDER_LEGACY_TILES 4

// POKEKYOTO_GENERAL_FLOWER_RESTORE_V1
// Reuse the original 16x16 GBA flower frames (no new art or palettes).
#define GENERAL_FLOWER_FRAME_TICKS 16
static const u32 sKyotoGeneralFlowerFrame0[] = INCGFX_U32("data/tilesets/primary/general/anim/flower/0.png", ".4bpp");
static const u32 sKyotoGeneralFlowerFrame1[] = INCGFX_U32("data/tilesets/primary/general/anim/flower/1.png", ".4bpp");
static const u32 sKyotoGeneralFlowerFrame2[] = INCGFX_U32("data/tilesets/primary/general/anim/flower/2.png", ".4bpp");
static const u32 *const sKyotoGeneralFlowerFrames[] = {
    sKyotoGeneralFlowerFrame0,
    sKyotoGeneralFlowerFrame1,
    sKyotoGeneralFlowerFrame0,
    sKyotoGeneralFlowerFrame2,
};

static const u32 sPetalburgLavenderFrame0[] = INCGFX_U32("graphics/tileset_anims/petalburg/lavender/0.png", ".4bpp");
static const u32 sPetalburgLavenderFrame1[] = INCGFX_U32("graphics/tileset_anims/petalburg/lavender/1.png", ".4bpp");
static const u32 sPetalburgLavenderFrame2[] = INCGFX_U32("graphics/tileset_anims/petalburg/lavender/2.png", ".4bpp");
static const u32 sPetalburgLavenderFrame3[] = INCGFX_U32("graphics/tileset_anims/petalburg/lavender/3.png", ".4bpp");

static const u32 *const sPetalburgLavenderFrames[] = {
    sPetalburgLavenderFrame0,
    sPetalburgLavenderFrame1,
    sPetalburgLavenderFrame2,
    sPetalburgLavenderFrame3,
};

static const u32 sPetalburgLavenderLargeFrame0[] = INCGFX_U32("graphics/tileset_anims/petalburg/lavender_large/0.png", ".4bpp");
static const u32 sPetalburgLavenderLargeFrame1[] = INCGFX_U32("graphics/tileset_anims/petalburg/lavender_large/1.png", ".4bpp");
static const u32 sPetalburgLavenderLargeFrame2[] = INCGFX_U32("graphics/tileset_anims/petalburg/lavender_large/2.png", ".4bpp");
static const u32 sPetalburgLavenderLargeFrame3[] = INCGFX_U32("graphics/tileset_anims/petalburg/lavender_large/3.png", ".4bpp");
static const u32 *const sPetalburgLavenderLargeFrames[] = {
    sPetalburgLavenderLargeFrame0, sPetalburgLavenderLargeFrame1,
    sPetalburgLavenderLargeFrame2, sPetalburgLavenderLargeFrame3,
};

// Local 8x8 tile indices, not VRAM slots. The 16x16 flower is at (0, 640)
// in Petalburg's 128px-wide sheet. tilesetcap.py reads this table to keep
// animated tiles out of static deduplication, including as alias targets.
static const struct {
    const struct Tileset *tileset;
    u16 tile;
    u16 frameTile;
} sStreamedAnimTiles[] = {
    // Primary General's original Emerald flower occupies local tiles 508-511.
    { &gTileset_General, 508, 0 },
    { &gTileset_General, 509, 1 },
    { &gTileset_General, 510, 2 },
    { &gTileset_General, 511, 3 },
    { &gTileset_Petalburg, 1280, 0 },
    { &gTileset_Petalburg, 1281, 1 },
    { &gTileset_Petalburg, 1296, 2 },
    { &gTileset_Petalburg, 1297, 3 },
    // Frame-tile 4 onward selects the padded 16x32 artwork.
    { &gTileset_Petalburg, 1996, 4 },
    { &gTileset_Petalburg, 1997, 5 },
    { &gTileset_Petalburg, 2012, 6 },
    { &gTileset_Petalburg, 2013, 7 },
    { &gTileset_Petalburg, 2028, 8 },
    { &gTileset_Petalburg, 2029, 9 },
    { &gTileset_Petalburg, 2044, 10 },
    { &gTileset_Petalburg, 2045, 11 },
};

static EWRAM_DATA struct {
    const u16 *src;
    u16 *dest;
    u16 size;
} sTilesetDMA3TransferBuffer[20] = {0};

static u8 sTilesetDMA3TransferBufferSize;
static u16 sPrimaryTilesetAnimCounter;
static u16 sPrimaryTilesetAnimCounterMax;
static u16 sSecondaryTilesetAnimCounter;
static u16 sSecondaryTilesetAnimCounterMax;
static void (*sPrimaryTilesetAnimCallback)(u16);
static void (*sSecondaryTilesetAnimCallback)(u16);
static u32 *sAnimDiscoverySlots;

static void _InitPrimaryTilesetAnimation(void);
static void _InitSecondaryTilesetAnimation(void);

/*
 * PokeKyoto - clean tileset animation base.
 *
 * All original Emerald, FRLG and TileCacheTest animations remain disabled.
 * Petalburg's lavender uses logical tile sources instead of fixed VRAM slots.
 * Its existing metatiles select palette 12; no palette is changed here.
 *
 * The transfer buffer and tile-cache discovery are kept for future animations.
 * To add a new animation later:
 *   1. Add its frame data and an animation callback in this file.
 *   2. Queue tile graphics through AppendTilesetAnimToBuffer so the cache can
 *      discover and protect the animation's physical VRAM destinations.
 *   3. Assign the callback in the appropriate InitTilesetAnim_* function and
 *      set its counter period. Leave other tilesets' callbacks as NULL.
 *
 * Expanded logical tile IDs are not direct VRAM destination indices.
 */

const u32 *TilesetAnims_GetTileSource(const struct Tileset *tileset, u32 tile)
{
    for (u32 i = 0; i < ARRAY_COUNT(sStreamedAnimTiles); i++)
    {
        if (sStreamedAnimTiles[i].tileset == tileset && sStreamedAnimTiles[i].tile == tile)
        {
            // The cache updates all four original 8x8 tiles together. This
            // also makes a flower entering the screen show the current frame.
            if (tileset == &gTileset_General)
            {
                u32 frame = (sPrimaryTilesetAnimCounter / GENERAL_FLOWER_FRAME_TICKS)
                          % ARRAY_COUNT(sKyotoGeneralFlowerFrames);
                return sKyotoGeneralFlowerFrames[frame]
                     + sStreamedAnimTiles[i].frameTile * (TILE_SIZE_4BPP / sizeof(u32));
            }
            if (sStreamedAnimTiles[i].frameTile >= LAVENDER_LEGACY_TILES)
            {
                u32 frame = (sSecondaryTilesetAnimCounter / LAVENDER_LARGE_FRAME_TICKS) % ARRAY_COUNT(sPetalburgLavenderLargeFrames);
                return sPetalburgLavenderLargeFrames[frame] + (sStreamedAnimTiles[i].frameTile - LAVENDER_LEGACY_TILES) * (TILE_SIZE_4BPP / sizeof(u32));
            }
            u32 frame = (sSecondaryTilesetAnimCounter / LAVENDER_FRAME_TICKS) % ARRAY_COUNT(sPetalburgLavenderFrames);
            return sPetalburgLavenderFrames[frame] + sStreamedAnimTiles[i].frameTile * (TILE_SIZE_4BPP / sizeof(u32));
        }
    }
    return NULL;
}

static void ResetTilesetAnimBuffer(void)
{
    sTilesetDMA3TransferBufferSize = 0;
    CpuFill32(0, sTilesetDMA3TransferBuffer, sizeof sTilesetDMA3TransferBuffer);
}

static void UNUSED AppendTilesetAnimToBuffer(const u16 *src, u16 *dest, u16 size)
{
    u32 firstSlot = ((uintptr_t)dest - BG_VRAM) / TILE_SIZE_4BPP;
    u32 numSlots = (size + TILE_SIZE_4BPP - 1) / TILE_SIZE_4BPP;

    if (sAnimDiscoverySlots != NULL)
    {
        for (u32 slot = firstSlot; slot < firstSlot + numSlots && slot < NUM_TILES_TOTAL; slot++)
            sAnimDiscoverySlots[slot / 32] |= 1u << (slot % 32);
        return;
    }

    if (!TileCache_AllowsAnimDest(firstSlot, numSlots))
        return;

    if (sTilesetDMA3TransferBufferSize < 20)
    {
        sTilesetDMA3TransferBuffer[sTilesetDMA3TransferBufferSize].src = src;
        sTilesetDMA3TransferBuffer[sTilesetDMA3TransferBufferSize].dest = dest;
        sTilesetDMA3TransferBuffer[sTilesetDMA3TransferBufferSize].size = size;
        sTilesetDMA3TransferBufferSize ++;
    }
}

void TransferTilesetAnimsBuffer(void)
{
    int i;

    for (i = 0; i < sTilesetDMA3TransferBufferSize; i ++)
        DmaCopy16(3, sTilesetDMA3TransferBuffer[i].src, sTilesetDMA3TransferBuffer[i].dest, sTilesetDMA3TransferBuffer[i].size);

    sTilesetDMA3TransferBufferSize = 0;

    // Resolve the cache owner in VBlank, after the frame's scrolling/allocations.
    // Newly loaded tiles use the same frame through TilesetAnims_GetTileSource.
    for (i = 0; i < ARRAY_COUNT(sStreamedAnimTiles); i++)
        TileCache_TransferAnimatedTile(sStreamedAnimTiles[i].tileset, sStreamedAnimTiles[i].tile,
            TilesetAnims_GetTileSource(sStreamedAnimTiles[i].tileset, sStreamedAnimTiles[i].tile));
}

void InitTilesetAnimations(void)
{
    ResetTilesetAnimBuffer();
    _InitPrimaryTilesetAnimation();
    _InitSecondaryTilesetAnimation();
}

void InitSecondaryTilesetAnimation(void)
{
    _InitSecondaryTilesetAnimation();
}

static void RecordAnimVramSlots(void (*callback)(u16), u32 counterMax, u32 *slots)
{
    if (callback == NULL || slots == NULL)
        return;

    sAnimDiscoverySlots = slots;
    for (u32 timer = 0; timer < counterMax; timer++)
        callback(timer);
    sAnimDiscoverySlots = NULL;
}

void TilesetAnims_DiscoverVramSlots(const struct MapLayout *layout, u32 *primarySlots, u32 *secondarySlots)
{
    u16 primaryCounter = sPrimaryTilesetAnimCounter;
    u16 primaryCounterMax = sPrimaryTilesetAnimCounterMax;
    u16 secondaryCounter = sSecondaryTilesetAnimCounter;
    u16 secondaryCounterMax = sSecondaryTilesetAnimCounterMax;
    void (*primaryCallback)(u16) = sPrimaryTilesetAnimCallback;
    void (*secondaryCallback)(u16) = sSecondaryTilesetAnimCallback;

    sPrimaryTilesetAnimCounterMax = 0;
    sPrimaryTilesetAnimCallback = NULL;
    sSecondaryTilesetAnimCounterMax = 0;
    sSecondaryTilesetAnimCallback = NULL;
    if (layout->primaryTileset != NULL && layout->primaryTileset->callback != NULL)
        layout->primaryTileset->callback();
    if (layout->secondaryTileset != NULL && layout->secondaryTileset->callback != NULL)
        layout->secondaryTileset->callback();
    RecordAnimVramSlots(sPrimaryTilesetAnimCallback, sPrimaryTilesetAnimCounterMax, primarySlots);
    RecordAnimVramSlots(sSecondaryTilesetAnimCallback, sSecondaryTilesetAnimCounterMax, secondarySlots);

    sPrimaryTilesetAnimCounter = primaryCounter;
    sPrimaryTilesetAnimCounterMax = primaryCounterMax;
    sSecondaryTilesetAnimCounter = secondaryCounter;
    sSecondaryTilesetAnimCounterMax = secondaryCounterMax;
    sPrimaryTilesetAnimCallback = primaryCallback;
    sSecondaryTilesetAnimCallback = secondaryCallback;
}

void UpdateTilesetAnimations(void)
{
    ResetTilesetAnimBuffer();
    if (++sPrimaryTilesetAnimCounter >= sPrimaryTilesetAnimCounterMax)
        sPrimaryTilesetAnimCounter = 0;
    if (++sSecondaryTilesetAnimCounter >= sSecondaryTilesetAnimCounterMax)
        sSecondaryTilesetAnimCounter = 0;

    if (sPrimaryTilesetAnimCallback)
        sPrimaryTilesetAnimCallback(sPrimaryTilesetAnimCounter);
    if (sSecondaryTilesetAnimCallback)
        sSecondaryTilesetAnimCallback(sSecondaryTilesetAnimCounter);
}

static void _InitPrimaryTilesetAnimation(void)
{
    sPrimaryTilesetAnimCounter = 0;
    sPrimaryTilesetAnimCounterMax = 0;
    sPrimaryTilesetAnimCallback = NULL;
    if (gMapHeader.mapLayout->primaryTileset && gMapHeader.mapLayout->primaryTileset->callback)
        gMapHeader.mapLayout->primaryTileset->callback();
}

static void _InitSecondaryTilesetAnimation(void)
{
    sSecondaryTilesetAnimCounter = 0;
    sSecondaryTilesetAnimCounterMax = 0;
    sSecondaryTilesetAnimCallback = NULL;
    if (gMapHeader.mapLayout->secondaryTileset && gMapHeader.mapLayout->secondaryTileset->callback)
        gMapHeader.mapLayout->secondaryTileset->callback();
}

// Existing tileset entry points. Only Petalburg's streamed lavender is enabled.

void InitTilesetAnim_General(void)
{
    sPrimaryTilesetAnimCounter = 0;
    sPrimaryTilesetAnimCounterMax = 256;
    sPrimaryTilesetAnimCallback = NULL;
}

void InitTilesetAnim_Building(void)
{
    sPrimaryTilesetAnimCounter = 0;
    sPrimaryTilesetAnimCounterMax = 256;
    sPrimaryTilesetAnimCallback = NULL;
}

void InitTilesetAnim_Petalburg(void)
{
    sSecondaryTilesetAnimCounter = 0;
    sSecondaryTilesetAnimCounterMax = LAVENDER_LARGE_FRAME_TICKS * ARRAY_COUNT(sPetalburgLavenderLargeFrames);
    sSecondaryTilesetAnimCallback = NULL;
}

void InitTilesetAnim_Rustboro(void)
{
    sSecondaryTilesetAnimCounter = 0;
    sSecondaryTilesetAnimCounterMax = sPrimaryTilesetAnimCounterMax;
    sSecondaryTilesetAnimCallback = NULL;
}

void InitTilesetAnim_Dewford(void)
{
    sSecondaryTilesetAnimCounter = 0;
    sSecondaryTilesetAnimCounterMax = sPrimaryTilesetAnimCounterMax;
    sSecondaryTilesetAnimCallback = NULL;
}

void InitTilesetAnim_Slateport(void)
{
    sSecondaryTilesetAnimCounter = 0;
    sSecondaryTilesetAnimCounterMax = sPrimaryTilesetAnimCounterMax;
    sSecondaryTilesetAnimCallback = NULL;
}

void InitTilesetAnim_Mauville(void)
{
    sSecondaryTilesetAnimCounter = sPrimaryTilesetAnimCounter;
    sSecondaryTilesetAnimCounterMax = sPrimaryTilesetAnimCounterMax;
    sSecondaryTilesetAnimCallback = NULL;
}

void InitTilesetAnim_Lavaridge(void)
{
    sSecondaryTilesetAnimCounter = 0;
    sSecondaryTilesetAnimCounterMax = sPrimaryTilesetAnimCounterMax;
    sSecondaryTilesetAnimCallback = NULL;
}

void InitTilesetAnim_Fallarbor(void)
{
    sSecondaryTilesetAnimCounter = 0;
    sSecondaryTilesetAnimCounterMax = sPrimaryTilesetAnimCounterMax;
    sSecondaryTilesetAnimCallback = NULL;
}

void InitTilesetAnim_Fortree(void)
{
    sSecondaryTilesetAnimCounter = 0;
    sSecondaryTilesetAnimCounterMax = sPrimaryTilesetAnimCounterMax;
    sSecondaryTilesetAnimCallback = NULL;
}

void InitTilesetAnim_Lilycove(void)
{
    sSecondaryTilesetAnimCounter = 0;
    sSecondaryTilesetAnimCounterMax = sPrimaryTilesetAnimCounterMax;
    sSecondaryTilesetAnimCallback = NULL;
}

void InitTilesetAnim_Mossdeep(void)
{
    sSecondaryTilesetAnimCounter = 0;
    sSecondaryTilesetAnimCounterMax = sPrimaryTilesetAnimCounterMax;
    sSecondaryTilesetAnimCallback = NULL;
}

void InitTilesetAnim_EverGrande(void)
{
    sSecondaryTilesetAnimCounter = 0;
    sSecondaryTilesetAnimCounterMax = sPrimaryTilesetAnimCounterMax;
    sSecondaryTilesetAnimCallback = NULL;
}

void InitTilesetAnim_Pacifidlog(void)
{
    sSecondaryTilesetAnimCounter = sPrimaryTilesetAnimCounter;
    sSecondaryTilesetAnimCounterMax = sPrimaryTilesetAnimCounterMax;
    sSecondaryTilesetAnimCallback = NULL;
}

void InitTilesetAnim_Sootopolis(void)
{
    sSecondaryTilesetAnimCounter = 0;
    sSecondaryTilesetAnimCounterMax = sPrimaryTilesetAnimCounterMax;
    sSecondaryTilesetAnimCallback = NULL;
}

void InitTilesetAnim_BattleFrontierOutsideWest(void)
{
    sSecondaryTilesetAnimCounter = 0;
    sSecondaryTilesetAnimCounterMax = sPrimaryTilesetAnimCounterMax;
    sSecondaryTilesetAnimCallback = NULL;
}

void InitTilesetAnim_BattleFrontierOutsideEast(void)
{
    sSecondaryTilesetAnimCounter = 0;
    sSecondaryTilesetAnimCounterMax = sPrimaryTilesetAnimCounterMax;
    sSecondaryTilesetAnimCallback = NULL;
}

void InitTilesetAnim_Underwater(void)
{
    sSecondaryTilesetAnimCounter = 0;
    sSecondaryTilesetAnimCounterMax = 128;
    sSecondaryTilesetAnimCallback = NULL;
}

void InitTilesetAnim_SootopolisGym(void)
{
    sSecondaryTilesetAnimCounter = 0;
    sSecondaryTilesetAnimCounterMax = 240;
    sSecondaryTilesetAnimCallback = NULL;
}

void InitTilesetAnim_Cave(void)
{
    sSecondaryTilesetAnimCounter = 0;
    sSecondaryTilesetAnimCounterMax = sPrimaryTilesetAnimCounterMax;
    sSecondaryTilesetAnimCallback = NULL;
}

void InitTilesetAnim_EliteFour(void)
{
    sSecondaryTilesetAnimCounter = 0;
    sSecondaryTilesetAnimCounterMax = 128;
    sSecondaryTilesetAnimCallback = NULL;
}

void InitTilesetAnim_MauvilleGym(void)
{
    sSecondaryTilesetAnimCounter = 0;
    sSecondaryTilesetAnimCounterMax = sPrimaryTilesetAnimCounterMax;
    sSecondaryTilesetAnimCallback = NULL;
}

void InitTilesetAnim_BikeShop(void)
{
    sSecondaryTilesetAnimCounter = 0;
    sSecondaryTilesetAnimCounterMax = sPrimaryTilesetAnimCounterMax;
    sSecondaryTilesetAnimCallback = NULL;
}

void InitTilesetAnim_BattlePyramid(void)
{
    sSecondaryTilesetAnimCounter = 0;
    sSecondaryTilesetAnimCounterMax = sPrimaryTilesetAnimCounterMax;
    sSecondaryTilesetAnimCallback = NULL;
}

void InitTilesetAnim_BattleDome(void)
{
    sSecondaryTilesetAnimCounter = 0;
    sSecondaryTilesetAnimCounterMax = sPrimaryTilesetAnimCounterMax;
    sSecondaryTilesetAnimCallback = NULL;
}

void InitTilesetAnim_General_Frlg(void)
{
    sPrimaryTilesetAnimCounter = 0;
    sPrimaryTilesetAnimCounterMax = 640;
    sPrimaryTilesetAnimCallback = NULL;
}

void InitTilesetAnim_CeladonCity(void)
{
    sSecondaryTilesetAnimCounter = 0;
    sSecondaryTilesetAnimCounterMax = 120;
    sSecondaryTilesetAnimCallback = NULL;
}

void InitTilesetAnim_SilphCo(void)
{
    sSecondaryTilesetAnimCounter = 0;
    sSecondaryTilesetAnimCounterMax = 160;
    sSecondaryTilesetAnimCallback = NULL;
}

void InitTilesetAnim_MtEmber(void)
{
    sSecondaryTilesetAnimCounter = 0;
    sSecondaryTilesetAnimCounterMax = 256;
    sSecondaryTilesetAnimCallback = NULL;
}

void InitTilesetAnim_VermilionGym(void)
{
    sSecondaryTilesetAnimCounter = 0;
    sSecondaryTilesetAnimCounterMax = 240;
    sSecondaryTilesetAnimCallback = NULL;
}

void InitTilesetAnim_CeladonGym(void)
{
    sSecondaryTilesetAnimCounter = 0;
    sSecondaryTilesetAnimCounterMax = 256;
    sSecondaryTilesetAnimCallback = NULL;
}

void InitTilesetAnim_TileCacheTest(void)
{
    sSecondaryTilesetAnimCounter = 0;
    sSecondaryTilesetAnimCounterMax = sPrimaryTilesetAnimCounterMax;
    sSecondaryTilesetAnimCallback = NULL;
}
