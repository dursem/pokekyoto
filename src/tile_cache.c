#include "global.h"
#include "opal_map_palette.h"
#include "tile_cache.h"
#include "bg.h"
#include "dma3.h"
#include "fieldmap.h"
#include "main.h"
#include "overworld.h"
#include "tileset_anims.h"
#include "data/tilesets/tile_cache_info.h"

#define SLOT_NONE 0
#define SLOT_BITMAP_WORDS ((TILE_CACHE_NUM_SLOTS + 31) / 32)
#define NUM_CACHED_VIRTUAL_TILES (NUM_VIRTUAL_TILES - NUM_TILES_IN_PRIMARY)
#define PENDING_RELEASE_EPOCHS 4

/*
 * A slot whose last tilemap reference is overwritten can't be reused straight away: the tilemap
 * copy that removes the reference from VRAM is only queued, and the DMA manager may delay it by
 * frames (it stops at 40 KiB per VBlank). Each frame's released slots therefore wait until every
 * DMA request queued by the end of that frame has been processed. M3 palette remapping is deliberately
 * not part of this M1+M2 package; when M3 is added its publication barrier extends this ticket rule.
 */
struct PendingReleases
{
    u32 slots[SLOT_BITMAP_WORDS];
    u32 dmaTicket;
    u32 tilemapTicket;
};

struct TileCache
{
    const struct TilesetCapacityInfo *primary;
    const struct TilesetCapacityInfo *secondary;
    u16 virtualToSlot[NUM_CACHED_VIRTUAL_TILES];
    u16 slotOwner[TILE_CACHE_NUM_SLOTS];
    u16 slotRefs[TILE_CACHE_NUM_SLOTS];
    u32 pinned[SLOT_BITMAP_WORDS];
    u32 primaryPinned[SLOT_BITMAP_WORDS];
    u32 pinPending[SLOT_BITMAP_WORDS];
    u32 releasedThisFrame[SLOT_BITMAP_WORDS];
    struct PendingReleases pending[PENDING_RELEASE_EPOCHS];
    u32 overflows;
    u16 cursor;
    u16 lastFrame;
    u8 numPending;
    bool8 active:1;
    bool8 redrawRequested:1;
    bool8 hasPendingPins:1;
    bool8 hasReleasesThisFrame:1;
};

static EWRAM_DATA struct TileCache sTileCache = {0};

/* KYOTO_OPAL_LEGACY_PADDING_BLANK
 * Vanilla Emerald tilesets sometimes reference unused slots in the legacy
 * 0..1023 physical tile window as transparent padding.  Those slots were
 * effectively blank in the stock renderer even when the PNG ended earlier.
 * A streamed map must preserve that behaviour instead of treating the
 * reference as a missing extended source tile.
 */
static const u32 sLegacyPaddingBlankTile[TILE_SIZE_4BPP / sizeof(u32)] = {0};

#define BIT_SET(bitmap, i)   ((bitmap)[(i) / 32] |= 1u << ((i) % 32))
#define BIT_CLEAR(bitmap, i) ((bitmap)[(i) / 32] &= ~(1u << ((i) % 32)))
#define BIT_TEST(bitmap, i)  ((bitmap)[(i) / 32] & (1u << ((i) % 32)))

const struct TilesetCapacityInfo *GetTilesetCapacityInfo(const struct Tileset *tileset)
{
    static const struct TilesetCapacityInfo *sLastFound = NULL;

    if (tileset == NULL)
        return NULL;
    if (sLastFound != NULL && sLastFound->tileset == tileset)
        return sLastFound;

    for (u32 i = 0; i < gTilesetCapacityInfoCount; i++)
    {
        if (gTilesetCapacityInfo[i].tileset == tileset)
        {
            sLastFound = &gTilesetCapacityInfo[i];
            return sLastFound;
        }
    }
    return NULL;
}

u32 GetTilesetNumMetatiles(const struct Tileset *tileset, u32 fallback)
{
    const struct TilesetCapacityInfo *info = GetTilesetCapacityInfo(tileset);
    return info != NULL ? info->numMetatiles : fallback;
}

bool32 TileCache_LayoutIsStreamed(const struct MapLayout *layout)
{
    const struct TilesetCapacityInfo *primary, *secondary;

    if (layout == NULL)
        return FALSE;

    primary = GetTilesetCapacityInfo(layout->primaryTileset);
    secondary = GetTilesetCapacityInfo(layout->secondaryTileset);
    if (primary == NULL || secondary == NULL || primary->rawTiles == NULL || secondary->rawTiles == NULL)
        return FALSE;
    return primary->streamed || secondary->streamed;
}

bool32 TileCache_IsActive(void)
{
    return sTileCache.active;
}

static inline bool32 IsPoolSlot(u32 slot)
{
    return slot >= TILE_CACHE_FIRST_SLOT && slot < TILE_CACHE_END_SLOT;
}

static bool32 IsReleasePending(u32 index)
{
    if (BIT_TEST(sTileCache.releasedThisFrame, index))
        return TRUE;
    for (u32 i = 0; i < sTileCache.numPending; i++)
    {
        if (BIT_TEST(sTileCache.pending[i].slots, index))
            return TRUE;
    }
    return FALSE;
}

static bool32 IsSlotFree(u32 index)
{
    return sTileCache.slotRefs[index] == 0
        && !BIT_TEST(sTileCache.pinned, index)
        && !BIT_TEST(sTileCache.pinPending, index)
        && !IsReleasePending(index);
}

static const u32 *GetTileSource(u32 tile)
{
    const struct TilesetCapacityInfo *info;
    u32 local;

    if (tile < NUM_TILES_TOTAL)
    {
        info = sTileCache.secondary;
        local = tile - NUM_TILES_IN_PRIMARY;
    }
    else if (tile < NUM_TILES_TOTAL + NUM_TILES_IN_PRIMARY)
    {
        info = sTileCache.primary;
        local = tile - NUM_TILES_IN_PRIMARY;
    }
    else
    {
        info = sTileCache.secondary;
        local = tile - NUM_TILES_TOTAL;
    }

    if (info == NULL || info->rawTiles == NULL)
        return NULL;

    if (local >= info->numTiles)
    {
        // Preserve stock Emerald's transparent padding in the physical 0..1023 tile window.
        if (tile < NUM_TILES_TOTAL)
            return sLegacyPaddingBlankTile;
        return NULL;
    }

    return info->rawTiles + local * (TILE_SIZE_4BPP / sizeof(u32));
}

static inline void *SlotVram(u32 slot)
{
    return (void *)(BG_VRAM + TILE_OFFSET_4BPP(slot));
}

static void ForgetOwner(u32 index)
{
    u32 owner = sTileCache.slotOwner[index];

    if (owner != 0 && sTileCache.virtualToSlot[owner - NUM_TILES_IN_PRIMARY] == index + TILE_CACHE_FIRST_SLOT)
        sTileCache.virtualToSlot[owner - NUM_TILES_IN_PRIMARY] = SLOT_NONE;
    sTileCache.slotOwner[index] = 0;
}

// Pinned slots hold the tile tileset_anims.c animates there, which is the tile the slot held before tilesets could stream.
static void PinSlot(u32 index)
{
    u32 slot = index + TILE_CACHE_FIRST_SLOT;
    u32 previous = sTileCache.virtualToSlot[slot - NUM_TILES_IN_PRIMARY];
    const u32 *src = GetTileSource(slot);

    if (previous != SLOT_NONE && previous != slot)
        sTileCache.slotOwner[previous - TILE_CACHE_FIRST_SLOT] = 0;
    ForgetOwner(index);
    sTileCache.slotOwner[index] = slot;
    sTileCache.virtualToSlot[slot - NUM_TILES_IN_PRIMARY] = slot;
    BIT_SET(sTileCache.pinned, index);
    BIT_CLEAR(sTileCache.pinPending, index);
    if (src != NULL)
        CpuFastCopy(src, SlotVram(slot), TILE_SIZE_4BPP);
    else
        CpuFastFill(0, SlotVram(slot), TILE_SIZE_4BPP);
}

static void ActivatePendingPins(void)
{
    bool32 stillPending = FALSE;

    for (u32 index = 0; index < TILE_CACHE_NUM_SLOTS; index++)
    {
        if (!BIT_TEST(sTileCache.pinPending, index))
            continue;
        if (sTileCache.slotRefs[index] == 0 && !IsReleasePending(index))
            PinSlot(index);
        else
            stillPending = TRUE;
    }
    sTileCache.hasPendingPins = stillPending;
}

static void SealReleasesThisFrame(void)
{
    struct PendingReleases *target;

    if (!sTileCache.hasReleasesThisFrame)
        return;

    if (sTileCache.numPending < PENDING_RELEASE_EPOCHS)
    {
        target = &sTileCache.pending[sTileCache.numPending++];
        CpuFill32(0, target->slots, sizeof(target->slots));
    }
    else
    {
        target = &sTileCache.pending[PENDING_RELEASE_EPOCHS - 1];
    }

    for (u32 i = 0; i < SLOT_BITMAP_WORDS; i++)
        target->slots[i] |= sTileCache.releasedThisFrame[i];
    target->dmaTicket = GetDma3RequestsQueued();
    target->tilemapTicket = OpalMapPalettesTilemapsPrepared();
    CpuFill32(0, sTileCache.releasedThisFrame, sizeof(sTileCache.releasedThisFrame));
    sTileCache.hasReleasesThisFrame = FALSE;
}

static void CommitReleases(void)
{
    u32 dmaDone = GetDma3RequestsDone();
    u32 tilemapsDone = OpalMapPalettesTilemapsCommitted();
    u32 committed = 0;

    while (committed < sTileCache.numPending
        && (s32)(dmaDone - sTileCache.pending[committed].dmaTicket) >= 0
        && (s32)(tilemapsDone - sTileCache.pending[committed].tilemapTicket) >= 0)
        committed++;

    if (committed == 0)
        return;

    for (u32 i = committed; i < sTileCache.numPending; i++)
        sTileCache.pending[i - committed] = sTileCache.pending[i];
    sTileCache.numPending -= committed;
}

static void TickIfNewFrame(void)
{
    if ((u16)gMain.vblankCounter1 == sTileCache.lastFrame)
        return;

    sTileCache.lastFrame = gMain.vblankCounter1;
    SealReleasesThisFrame();
    CommitReleases();
    if (sTileCache.hasPendingPins)
        ActivatePendingPins();
}

static u32 AllocateSlot(u32 preferredSlot)
{
    if (IsPoolSlot(preferredSlot) && IsSlotFree(preferredSlot - TILE_CACHE_FIRST_SLOT))
        return preferredSlot;

    for (u32 i = 0; i < TILE_CACHE_NUM_SLOTS; i++)
    {
        u32 index = sTileCache.cursor;
        if (++sTileCache.cursor >= TILE_CACHE_NUM_SLOTS)
            sTileCache.cursor = 0;
        if (IsSlotFree(index))
            return index + TILE_CACHE_FIRST_SLOT;
    }
    return SLOT_NONE;
}

static u32 AcquireTile(u32 tile)
{
    u32 slot, index;
    const u32 *src;

    if (tile < NUM_TILES_IN_PRIMARY)
        return tile;
    if (tile >= NUM_VIRTUAL_TILES)
        return 0;

    slot = sTileCache.virtualToSlot[tile - NUM_TILES_IN_PRIMARY];
    if (slot != SLOT_NONE)
    {
        // Animation slots outside the allocatable pool are direct-mapped and never reused by the cache.
        if (IsPoolSlot(slot))
        {
            index = slot - TILE_CACHE_FIRST_SLOT;
            if (!BIT_TEST(sTileCache.pinned, index))
                sTileCache.slotRefs[index]++;
        }
        return slot;
    }

    src = GetTileSource(tile);
    assertf(src != NULL, "Tile cache: virtual tile %u is outside its tileset", tile)
    {
        return 0;
    }

    slot = AllocateSlot(tile < NUM_TILES_TOTAL ? tile : SLOT_NONE);
    // tilesetcap.py rejects maps that can need more slots than the cache has, so this is content the build could not see,
    // such as metatiles placed by scripts. The cell shows tile 0 rather than a tile another cell still uses.
    if (slot == SLOT_NONE)
    {
        sTileCache.overflows++;
        DebugPrintf("Tile cache full: no slot for tile %u", tile);
        return 0;
    }

    index = slot - TILE_CACHE_FIRST_SLOT;
    ForgetOwner(index);
    sTileCache.slotOwner[index] = tile;
    sTileCache.slotRefs[index] = 1;
    sTileCache.virtualToSlot[tile - NUM_TILES_IN_PRIMARY] = slot;
    // The slot isn't referenced by any tilemap VRAM holds, so it can be written mid-frame.
    CpuFastCopy(src, SlotVram(slot), TILE_SIZE_4BPP);
    return slot;
}

static void ReleaseSlot(u32 slot)
{
    u32 index;

    if (!IsPoolSlot(slot))
        return;

    index = slot - TILE_CACHE_FIRST_SLOT;
    if (BIT_TEST(sTileCache.pinned, index) || sTileCache.slotRefs[index] == 0)
        return;

    if (--sTileCache.slotRefs[index] == 0)
    {
        BIT_SET(sTileCache.releasedThisFrame, index);
        sTileCache.hasReleasesThisFrame = TRUE;
    }
}

static inline u32 GetVirtualTile(u16 tile, u32 ext)
{
    return (tile & TILEMAP_TILE_NUM_MASK) | (((ext & METATILE_EXT_TILE_HIGH_MASK) >> METATILE_EXT_TILE_HIGH_SHIFT) << 10);
}

void TileCache_WriteCell(u16 *cell, u16 tile, u32 ext)
{
    u32 slot;

    TickIfNewFrame();
    // Acquire before releasing, so rewriting a cell with the tile it already shows keeps the slot.
    slot = AcquireTile(GetVirtualTile(tile, ext));
    ReleaseSlot(*cell & TILEMAP_TILE_NUM_MASK);
    *cell = (tile & ~TILEMAP_TILE_NUM_MASK) | slot;
}

void TileCache_WriteCellPhysical(u16 *cell, u16 tile)
{
    u32 slot = tile & TILEMAP_TILE_NUM_MASK;

    TickIfNewFrame();
    if (IsPoolSlot(slot) && !BIT_TEST(sTileCache.pinned, slot - TILE_CACHE_FIRST_SLOT))
        sTileCache.slotRefs[slot - TILE_CACHE_FIRST_SLOT]++;
    ReleaseSlot(*cell & TILEMAP_TILE_NUM_MASK);
    *cell = tile;
}

u16 TileCache_Resolve(u16 tile, u32 ext)
{
    u32 virtualTile, slot;

    if (!sTileCache.active)
        return tile;

    virtualTile = GetVirtualTile(tile, ext);
    if (virtualTile < NUM_TILES_IN_PRIMARY)
        return tile;

    slot = virtualTile < NUM_VIRTUAL_TILES ? sTileCache.virtualToSlot[virtualTile - NUM_TILES_IN_PRIMARY] : SLOT_NONE;
    return (tile & ~TILEMAP_TILE_NUM_MASK) | slot;
}

const u8 *TileCache_GetMetatileExt(const struct Tileset *tileset, u32 metatile)
{
    const struct TilesetCapacityInfo *info;

    if (!sTileCache.active)
        return NULL;

    if (sTileCache.primary != NULL && sTileCache.primary->tileset == tileset)
        info = sTileCache.primary;
    else if (sTileCache.secondary != NULL && sTileCache.secondary->tileset == tileset)
        info = sTileCache.secondary;
    else
        return NULL;

    if (info->tileExt == NULL || metatile >= info->numMetatiles)
        return NULL;
    return info->tileExt + metatile * NUM_TILES_PER_METATILE;
}

const u16 *TileCache_GetMetatileThirdLayer(const struct Tileset *tileset, u32 metatile)
{
    const struct TilesetCapacityInfo *info = GetTilesetCapacityInfo(tileset);

    if (info == NULL || info->thirdLayer == NULL || metatile >= info->numMetatiles)
        return NULL;

    return info->thirdLayer + metatile * NUM_TILES_PER_METATILE_LAYER;
}

const u8 *TileCache_GetMetatileThirdLayerExt(const struct Tileset *tileset, u32 metatile)
{
    const struct TilesetCapacityInfo *info = GetTilesetCapacityInfo(tileset);

    if (info == NULL || info->thirdLayerExt == NULL || metatile >= info->numMetatiles)
        return NULL;

    return info->thirdLayerExt + metatile * NUM_TILES_PER_METATILE_LAYER;
}

bool32 TileCache_AllowsAnimDest(u32 firstSlot, u32 numSlots)
{
    if (!sTileCache.active)
        return TRUE;

    for (u32 slot = firstSlot; slot < firstSlot + numSlots; slot++)
    {
        if (slot < TILE_CACHE_FIRST_SLOT)
            continue;
        // Slots above the allocation pool are reserved from cache reuse. Stock animation/door/shop
        // behavior may still write them exactly as it did before streaming was enabled.
        if (!IsPoolSlot(slot))
            continue;
        if (!BIT_TEST(sTileCache.pinned, slot - TILE_CACHE_FIRST_SLOT))
            return FALSE;
    }
    return TRUE;
}

static void SlotsToPins(const u32 *slots, u32 *pins)
{
    for (u32 index = 0; index < TILE_CACHE_NUM_SLOTS; index++)
    {
        if (BIT_TEST(slots, index + TILE_CACHE_FIRST_SLOT))
            BIT_SET(pins, index);
    }
}

static void DirectMapExternalSecondaryAnimSlots(const u32 *slots)
{
    for (u32 slot = TILE_CACHE_END_SLOT; slot < NUM_TILES_TOTAL; slot++)
    {
        if (!BIT_TEST(slots, slot))
            continue;

        // A stock secondary animation that targets a reserved physical slot must keep that exact slot.
        // The cache never allocates this range, so the mapping is stable and cannot collide with a cache victim.
        const u32 *src = GetTileSource(slot);
        sTileCache.virtualToSlot[slot - NUM_TILES_IN_PRIMARY] = slot;
        if (src != NULL)
            CpuFastCopy(src, SlotVram(slot), TILE_SIZE_4BPP);
    }
}

static void DiscoverAnimatedSlots(const struct MapLayout *layout, u32 *primaryPins, u32 *secondaryPins)
{
    u32 primarySlots[NUM_TILES_TOTAL / 32] = {0};
    u32 secondarySlots[NUM_TILES_TOTAL / 32] = {0};

    TilesetAnims_DiscoverVramSlots(layout, primaryPins != NULL ? primarySlots : NULL, secondarySlots);
    if (primaryPins != NULL)
        SlotsToPins(primarySlots, primaryPins);
    SlotsToPins(secondarySlots, secondaryPins);
    DirectMapExternalSecondaryAnimSlots(secondarySlots);
}

static void RebuildRefsFromTilemaps(void)
{
    u16 *const tilemaps[] = {gOverworldTilemapBuffer_Bg1, gOverworldTilemapBuffer_Bg2, gOverworldTilemapBuffer_Bg3};

    CpuFill16(0, sTileCache.slotRefs, sizeof(sTileCache.slotRefs));
    for (u32 i = 0; i < ARRAY_COUNT(tilemaps); i++)
    {
        if (tilemaps[i] == NULL)
            continue;
        for (u32 cell = 0; cell < BG_SCREEN_SIZE / sizeof(u16); cell++)
        {
            u32 slot = tilemaps[i][cell] & TILEMAP_TILE_NUM_MASK;
            if (IsPoolSlot(slot))
                sTileCache.slotRefs[slot - TILE_CACHE_FIRST_SLOT]++;
        }
    }
}

static void RequestPins(const u32 *pins)
{
    for (u32 index = 0; index < TILE_CACHE_NUM_SLOTS; index++)
    {
        if (!BIT_TEST(pins, index))
            continue;
        if (sTileCache.slotRefs[index] == 0 && !IsReleasePending(index))
        {
            PinSlot(index);
        }
        else
        {
            BIT_SET(sTileCache.pinPending, index);
            sTileCache.hasPendingPins = TRUE;
        }
    }
}

static void ClearOverworldTilemaps(void)
{
    u16 *const tilemaps[] = {gOverworldTilemapBuffer_Bg1, gOverworldTilemapBuffer_Bg2, gOverworldTilemapBuffer_Bg3};

    for (u32 i = 0; i < ARRAY_COUNT(tilemaps); i++)
    {
        if (tilemaps[i] != NULL)
            CpuFill16(0, tilemaps[i], BG_SCREEN_SIZE);
    }
}

// Used while the screen is hidden: every cell is about to be drawn again.
void TileCache_LoadLayout(const struct MapLayout *layout)
{
    u32 secondaryPins[SLOT_BITMAP_WORDS] = {0};

    CpuFill32(0, &sTileCache, sizeof(sTileCache));
    sTileCache.primary = GetTilesetCapacityInfo(layout->primaryTileset);
    sTileCache.secondary = GetTilesetCapacityInfo(layout->secondaryTileset);
    sTileCache.lastFrame = gMain.vblankCounter1;
    sTileCache.active = TRUE;
    ClearOverworldTilemaps();

    DiscoverAnimatedSlots(layout, sTileCache.primaryPinned, secondaryPins);
    RequestPins(sTileCache.primaryPinned);
    RequestPins(secondaryPins);
}

// Used when walking into a connected map: cells of the previous map stay on screen until they scroll away.
void TileCache_SwitchLayout(const struct MapLayout *layout)
{
    u32 secondaryPins[SLOT_BITMAP_WORDS] = {0};
    bool32 wasActive = sTileCache.active;

    TickIfNewFrame();
    if (!wasActive)
    {
        CpuFill32(0, &sTileCache, sizeof(sTileCache));
        sTileCache.lastFrame = gMain.vblankCounter1;
        sTileCache.active = TRUE;
        sTileCache.primary = GetTilesetCapacityInfo(layout->primaryTileset);
    }
    else
    {
        for (u32 index = 0; index < TILE_CACHE_NUM_SLOTS; index++)
        {
            u32 owner = sTileCache.slotOwner[index];
            if (owner != 0 && (owner < NUM_TILES_TOTAL || owner >= NUM_TILES_TOTAL + NUM_TILES_IN_PRIMARY))
                ForgetOwner(index);
        }
        // Drop direct mappings for reserved secondary animation slots from the previous connected map.
        for (u32 tile = NUM_TILES_IN_PRIMARY; tile < NUM_TILES_TOTAL; tile++)
        {
            u32 slot = sTileCache.virtualToSlot[tile - NUM_TILES_IN_PRIMARY];
            if (slot >= TILE_CACHE_END_SLOT)
                sTileCache.virtualToSlot[tile - NUM_TILES_IN_PRIMARY] = SLOT_NONE;
        }
    }

    sTileCache.secondary = GetTilesetCapacityInfo(layout->secondaryTileset);
    CpuFill32(0, sTileCache.pinned, sizeof(sTileCache.pinned));
    CpuFill32(0, sTileCache.pinPending, sizeof(sTileCache.pinPending));
    sTileCache.hasPendingPins = FALSE;
    RebuildRefsFromTilemaps();

    DiscoverAnimatedSlots(layout, wasActive ? NULL : sTileCache.primaryPinned, secondaryPins);
    RequestPins(sTileCache.primaryPinned);
    RequestPins(secondaryPins);
}

bool32 TileCache_Disable(void)
{
    bool32 wasActive = sTileCache.active;

    sTileCache.active = FALSE;
    return wasActive;
}

void TileCache_RequestRedraw(void)
{
    sTileCache.redrawRequested = TRUE;
}

bool32 TileCache_TakeRedrawRequest(void)
{
    bool32 requested = sTileCache.redrawRequested;

    sTileCache.redrawRequested = FALSE;
    return requested;
}

u32 TileCache_GetOverflowCount(void)
{
    return sTileCache.overflows;
}
