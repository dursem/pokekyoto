#include "global.h"
#include "kyoto_lighting.h"
#include "kyoto_cloud_layers.h"
#include "opal_map_palette.h"
#include "bg.h"
#include "field_weather.h"
#include "malloc.h"
#include "overworld.h"
#include "palette.h"
#include "constants/field_weather.h"

_Static_assert(NUM_PALS_TOTAL == 13, "Kyoto Opal M3 requires 13 physical map palette banks");
_Static_assert(OPAL_MAP_PALETTES_COUNT == 18, "Kyoto Opal M3 requires 18 logical palettes");

struct OpalMapPaletteState
{
    const struct MapLayout *layout;
    const struct OpalTilesetPaletteExtension *extension;
    u8 references[3][OPAL_MAP_PALETTE_CELLS];
    u8 banks[OPAL_MAP_PALETTES_COUNT];
    u16 useCounts[OPAL_MAP_PALETTES_COUNT];
    u8 owners[OPAL_MAP_PHYSICAL_BANKS];
    u16 reserved;
    u16 pinned;
    u16 primaryMetatileCount;
    u32 resident;
    bool8 dirty;
    bool8 ready;
    u8 tileCopyCount;
    const void *tileSources[16];
    u16 tileIds[16];
};

static EWRAM_DATA struct OpalMapPaletteState *sState = NULL;
// Running totals of complete field tilemap states prepared for VBlank and actually transferred.
static u32 sTilemapsPrepared;
static u32 sTilemapsCommitted;
static volatile bool8 sInFrame;

#define REQUIRE_PALETTE(condition, ...) do { \
    if (!(condition)) { \
        OpalMapPalettesReset(); \
        fatalf(__VA_ARGS__); \
    } \
} while (0)

bool32 OpalMapPaletteAllocate(u32 required, u32 reserved, u32 pinned, const u8 *previous, u8 *result)
{
    u32 occupied = reserved & OPAL_MAP_PHYSICAL_MASK;
    required |= pinned;

    if ((required >> OPAL_MAP_PALETTES_COUNT)
     || (reserved & ~OPAL_MAP_PHYSICAL_MASK)
     || (pinned & ~OPAL_MAP_PHYSICAL_MASK)
     || (reserved & pinned))
        return FALSE;

    memset(result, OPAL_MAP_PALETTE_NONE, OPAL_MAP_PALETTES_COUNT);

    // Pinned palettes keep logical N in physical bank N. Kyoto only permits pinning 0..12.
    for (u32 logical = 0; logical < OPAL_MAP_PHYSICAL_BANKS; logical++)
    {
        if (pinned & (1u << logical))
        {
            result[logical] = logical;
            occupied |= 1u << logical;
        }
    }

    // Prefer the previous assignment to avoid needless tilemap/palette churn.
    for (u32 logical = 0; logical < OPAL_MAP_PALETTES_COUNT; logical++)
    {
        u32 bank = previous[logical];
        if ((required & (1u << logical))
         && result[logical] == OPAL_MAP_PALETTE_NONE
         && bank < OPAL_MAP_PHYSICAL_BANKS
         && !(occupied & (1u << bank)))
        {
            result[logical] = bank;
            occupied |= 1u << bank;
        }
    }

    // Allocate remaining required logical palettes to any free map bank 0..12.
    for (u32 logical = 0; logical < OPAL_MAP_PALETTES_COUNT; logical++)
    {
        if (!(required & (1u << logical)) || result[logical] != OPAL_MAP_PALETTE_NONE)
            continue;

        u32 bank;
        for (bank = 0; bank < OPAL_MAP_PHYSICAL_BANKS && (occupied & (1u << bank)); bank++);
        if (bank == OPAL_MAP_PHYSICAL_BANKS)
            return FALSE;

        result[logical] = bank;
        occupied |= 1u << bank;
    }

    return TRUE;
}

bool32 OpalMapPalettesActive(void)
{
    return sState != NULL;
}

u32 OpalMapPalettesMetatileCount(const struct Tileset *tileset, u32 fallback)
{
    if (!sState)
        return fallback;
    if (tileset == sState->extension->tileset)
        return sState->extension->metatileCount;
    if (tileset == sState->layout->primaryTileset)
        return sState->primaryMetatileCount;
    return fallback;
}

void OpalMapPalettesOnHeapReset(void)
{
    // Save-block relocation may already have overwritten the heap. Do not call Free here.
    sState = NULL;
    sTilemapsCommitted = sTilemapsPrepared;
}

void OpalMapPalettesReset(void)
{
    if (sState)
        Free(sState);
    sState = NULL;
    sTilemapsCommitted = sTilemapsPrepared;
}

void OpalMapPalettesLoad(const struct MapLayout *layout)
{
    const struct OpalTilesetPaletteExtension *extension = gOpalTilesetPaletteExtensions;
    const struct OpalMapPaletteProfile *profile = gOpalMapPaletteProfiles;

    while (extension->tileset && extension->tileset != layout->secondaryTileset)
        extension++;
    if (!extension->tileset)
    {
        OpalMapPalettesReset();
        return;
    }

    while (profile->layout && profile->layout != layout)
        profile++;
    REQUIRE_PALETTE(profile->layout, "Extended tileset used by an uncertified layout");

    OpalMapPalettesLoadProfile(layout, extension, profile);
}

void OpalMapPalettesLoadProfile(const struct MapLayout *layout,
                                const struct OpalTilesetPaletteExtension *extension,
                                const struct OpalMapPaletteProfile *profile)
{
    // Same-pair cardinal connections can preserve the existing transaction/cache assignment.
    if (sState && sState->extension == extension
     && sState->layout->primaryTileset == layout->primaryTileset)
    {
        sState->layout = layout;
        sState->reserved |= profile->reservedBanks & OPAL_MAP_PHYSICAL_MASK;
        sState->pinned |= profile->pinnedPalettes & OPAL_MAP_PHYSICAL_MASK;
        sState->resident |= profile->residentPalettes;
        sState->dirty = TRUE;
        return;
    }

    OpalMapPalettesReset();
    sState = AllocZeroed(sizeof(*sState));
    REQUIRE_PALETTE(sState, "Cannot allocate map palette state");

    sState->layout = layout;
    sState->extension = extension;
    sState->reserved = profile->reservedBanks & OPAL_MAP_PHYSICAL_MASK;
    sState->pinned = (profile->pinnedPalettes | 1u) & OPAL_MAP_PHYSICAL_MASK;
    sState->resident = profile->residentPalettes;
    sState->primaryMetatileCount = profile->primaryMetatileCount;
    memset(sState->references, OPAL_MAP_PALETTE_NONE, sizeof(sState->references));
    memset(sState->banks, OPAL_MAP_PALETTE_NONE, sizeof(sState->banks));
    memset(sState->owners, OPAL_MAP_PALETTE_NONE, sizeof(sState->owners));
    sState->dirty = TRUE;
}

static u16 *GetTilemap(u32 layer)
{
    switch (layer)
    {
    case 0: return gOverworldTilemapBuffer_Bg1;
    case 1: return gOverworldTilemapBuffer_Bg2;
    default: return gOverworldTilemapBuffer_Bg3;
    }
}

const u8 *OpalMapPalettesGetReferences(const struct Tileset *tileset, u32 metatile)
{
    if (!sState || tileset != sState->extension->tileset)
        return NULL;

    REQUIRE_PALETTE(metatile < sState->extension->metatileCount,
                    "Invalid extended metatile %u", metatile);
    return sState->extension->references + metatile * OPAL_MAP_REFS_PER_METATILE;
}

static void SetReference(u32 layer, u32 cell, u32 logical)
{
    REQUIRE_PALETTE(layer < 3 && cell < OPAL_MAP_PALETTE_CELLS && logical < OPAL_MAP_PALETTES_COUNT,
                    "Invalid map palette reference %u at layer %u cell %u", logical, layer, cell);

    u32 old = sState->references[layer][cell];
    if (old != logical)
    {
        if (old != OPAL_MAP_PALETTE_NONE)
        {
            REQUIRE_PALETTE(sState->useCounts[old] != 0, "Map palette use-count underflow");
            sState->useCounts[old]--;
        }
        sState->useCounts[logical]++;
        sState->references[layer][cell] = logical;
    }

    u16 *tilemap = GetTilemap(layer);
    u32 bank = sState->banks[logical];
    if (tilemap)
        tilemap[cell] = (tilemap[cell] & 0x0FFF)
                      | ((bank == OPAL_MAP_PALETTE_NONE ? 0 : bank) << 12);
}

static inline u32 LogicalForTile(const u16 *tiles, const u8 *references, u32 index)
{
    return references ? references[index] : (tiles[index] >> 12);
}

/*
 * Kyoto Emerald metatiles contain two 2x2 layers (8 subtile words), while the
 * field renderer exposes them over BG1/BG2/BG3 depending on layer type.
 * The palette tracker mirrors field_camera.c exactly:
 *
 *   SPLIT   : BG3=bottom[0..3], BG2=blank,  BG1=top[4..7]
 *   COVERED : BG3=bottom[0..3], BG2=top,    BG1=blank
 *   NORMAL  : BG3=stock 0x3014 filler, BG2=bottom, BG1=top
 */
void OpalMapPalettesSetMetatile(u32 offset, u32 layerType, const u16 *tiles, const u8 *references, const u16 *thirdTiles)
{
    static const u8 offsets[] = {0, 1, 32, 33};

    if (!sState)
        return;

    for (u32 i = 0; i < ARRAY_COUNT(offsets); i++)
    {
        u32 cell = offset + offsets[i];
        switch (layerType)
        {
        case METATILE_LAYER_TYPE_SPLIT:
            SetReference(2, cell, LogicalForTile(tiles, references, i));
            SetReference(1, cell, 0);
            SetReference(0, cell, LogicalForTile(tiles, references, 4 + i));
            break;
        case METATILE_LAYER_TYPE_TRIPLE:
            SetReference(2, cell, LogicalForTile(tiles, references, i));
            SetReference(1, cell, LogicalForTile(tiles, references, 4 + i));
            SetReference(
                0,
                cell,
                thirdTiles
                    ? LogicalForTile(
                          thirdTiles,
                          references ? references + NUM_TILES_PER_METATILE : NULL,
                          i)
                    : 0);
            break;
        case METATILE_LAYER_TYPE_COVERED:
            SetReference(2, cell, LogicalForTile(tiles, references, i));
            SetReference(1, cell, LogicalForTile(tiles, references, 4 + i));
            SetReference(0, cell, 0);
            break;
        case METATILE_LAYER_TYPE_NORMAL:
        default:
            // field_camera.c writes physical tile 0x14 with raw palette nibble 3 on BG3.
            SetReference(2, cell, 3);
            SetReference(1, cell, LogicalForTile(tiles, references, i));
            SetReference(0, cell, LogicalForTile(tiles, references, 4 + i));
            break;
        }
    }

    sState->dirty = TRUE;
}

static const u16 *GetBasePalette(u32 logical)
{
    REQUIRE_PALETTE(logical < OPAL_MAP_PALETTES_COUNT, "Invalid logical palette %u", logical);

    if (logical >= OPAL_MAP_BASE_LOGICAL_PALETTES)
        return sState->extension->extraPalettes[logical - OPAL_MAP_BASE_LOGICAL_PALETTES];

    u32 primaryCount = GetNumPalsInPrimary(sState->layout);
    if (logical < primaryCount)
        return sState->layout->primaryTileset->palettes[logical];

    // Logical 6..13 come from the secondary table. 13 is new to the logical map
    // library but already has an ordinary palettes/13.pal source file.
    return sState->layout->secondaryTileset->palettes[logical];
}

void OpalMapPalettesUpdateAlternates(u32 banks)
{
    if (!sState || !MapHasNaturalLight(gMapHeader.mapType))
        return;

    for (u32 bank = 1; bank < OPAL_MAP_PHYSICAL_BANKS; bank++)
    {
        u32 logical = sState->owners[bank];
        const u16 *night = NULL;

        if (!(banks & (1u << bank)) || logical == OPAL_MAP_PALETTE_NONE)
            continue;

        if (logical >= OPAL_MAP_BASE_LOGICAL_PALETTES)
        {
            night = sState->extension->extraNightPalettes[logical - OPAL_MAP_BASE_LOGICAL_PALETTES];
        }
        else if (logical < 13)
        {
            u32 primaryCount = GetNumPalsInPrimary(sState->layout);
            const struct Tileset *tileset = logical < primaryCount
                                         ? sState->layout->primaryTileset
                                         : sState->layout->secondaryTileset;
            u32 bit = logical < primaryCount ? logical : logical - primaryCount;
            if (tileset->swapPalettes & (1u << bit))
                night = tileset->palettes[(logical + 9) % 16];
        }
        // Logical 13 deliberately has no implicit night-alternate slot.

        if (night)
            AvgPaletteWeighted((u16 *)GetBasePalette(logical), (u16 *)night,
                               &gPlttBufferUnfaded[BG_PLTT_ID(bank)], gTimeBlend.altWeight);
    }
}

u32 OpalMapPalettesMask(void)
{
    return sState ? (PALETTES_MAP & ~sState->reserved) : PALETTES_MAP;
}

u32 OpalMapPalettesWeatherType(u32 bank, u32 fallback)
{
    if (!sState || bank >= OPAL_MAP_PHYSICAL_BANKS)
        return fallback;

    u32 logical = sState->owners[bank];
    if ((sState->reserved & (1u << bank)) || logical == OPAL_MAP_PALETTE_NONE)
        return COLOR_MAP_NONE;
    if (sState->pinned & (1u << bank))
        return fallback;
    if (logical >= OPAL_MAP_BASE_LOGICAL_PALETTES)
        return sState->extension->extraWeatherTypes[logical - OPAL_MAP_BASE_LOGICAL_PALETTES];

    return COLOR_MAP_DARK_CONTRAST;
}

static void Prepare(void)
{
    u8 next[OPAL_MAP_PALETTES_COUNT];
    u16 ALIGNED(4) unfaded[OPAL_MAP_PHYSICAL_BANKS][16];
    u16 ALIGNED(4) faded[OPAL_MAP_PHYSICAL_BANKS][16];
    u32 required = sState->pinned | sState->resident;
    u32 reserved = sState->reserved;
    u32 pinned = sState->pinned;

    for (u32 logical = 0; logical < OPAL_MAP_PALETTES_COUNT; logical++)
        if (sState->useCounts[logical])
            required |= 1u << logical;

    REQUIRE_PALETTE(OpalMapPaletteAllocate(required, reserved, pinned, sState->banks, next),
                    "Map palette overflow: need %x reserved %x pinned %x",
                    required, reserved, pinned);

    if (!memcmp(next, sState->banks, sizeof(next)))
    {
        sState->dirty = FALSE;
        sState->ready = TRUE;
        sTilemapsPrepared++;
        return;
    }

    CpuFastCopy(gPlttBufferUnfaded, unfaded, sizeof(unfaded));
    CpuFastCopy(gPlttBufferFaded, faded, sizeof(faded));
    memset(sState->owners, OPAL_MAP_PALETTE_NONE, sizeof(sState->owners));

    for (u32 logical = 0; logical < OPAL_MAP_PALETTES_COUNT; logical++)
        if (next[logical] != OPAL_MAP_PALETTE_NONE)
            sState->owners[next[logical]] = logical;

    for (u32 logical = 0; logical < OPAL_MAP_PALETTES_COUNT; logical++)
    {
        u32 bank = next[logical];
        u32 old = sState->banks[logical];

        if (bank == OPAL_MAP_PALETTE_NONE || bank == old)
            continue;

        if (old != OPAL_MAP_PALETTE_NONE)
        {
            CpuCopy16(unfaded[old], &gPlttBufferUnfaded[BG_PLTT_ID(bank)], PLTT_SIZE_4BPP);
            CpuCopy16(faded[old], &gPlttBufferFaded[BG_PLTT_ID(bank)], PLTT_SIZE_4BPP);
        }
        else
        {
            LoadPalette(GetBasePalette(logical), BG_PLTT_ID(bank), PLTT_SIZE_4BPP);
            if (logical == 0)
                gPlttBufferUnfaded[0] = gPlttBufferFaded[0] = 0;
            OpalMapPalettesUpdateAlternates(1u << bank);
            ApplyWeatherToNewMapPalette(bank);
        }
    }

    memcpy(sState->banks, next, sizeof(next));

    // Publish one self-consistent palette assignment into all field tilemaps.
    for (u32 layer = 0; layer < 3; layer++)
    {
        u16 *tilemap = GetTilemap(layer);
        if (!tilemap)
            continue;

        for (u32 cell = 0; cell < OPAL_MAP_PALETTE_CELLS; cell++)
        {
            u32 logical = sState->references[layer][cell];
            if (logical != OPAL_MAP_PALETTE_NONE)
            {
                u32 bank = next[logical];
                REQUIRE_PALETTE(bank != OPAL_MAP_PALETTE_NONE,
                                "Unallocated logical map palette %u", logical);
                tilemap[cell] = (tilemap[cell] & 0x0FFF) | (bank << 12);
            }
        }
    }

    sState->dirty = FALSE;
    sState->ready = TRUE;
    sTilemapsPrepared++;
}

void OpalMapPalettesBeginFrame(void)
{
    sInFrame = TRUE;
}

void OpalMapPalettesEndFrame(void)
{
    if (sState && sState->dirty)
        Prepare();
    KyotoLight_Prepare();
    KyotoCloud_Prepare();
    FireflyShade_Prepare();
    sInFrame = FALSE;
}

bool32 OpalMapPalettesBlockTransfer(void)
{
    // Admit a transaction only near the start of VBlank. A missed deadline repeats
    // the previous complete frame instead of publishing half of a palette/tilemap state.
    return sState && (sInFrame || REG_VCOUNT > 164
                  || (sState->ready && gPaletteFade.bufferTransferDisabled));
}

bool32 OpalMapPalettesQueueBg(u32 bg)
{
    if (!sState || bg < 1 || bg > 3 || GetBgTilemapBuffer(bg) != GetTilemap(bg - 1))
        return FALSE;

    sState->dirty = TRUE;
    return TRUE;
}

void OpalMapPalettesCommit(void)
{
    if (!sState || !sState->ready)
        return;

    for (u32 i = 0; i < sState->tileCopyCount; i++)
        DmaCopy16(3, sState->tileSources[i],
                  (void *)(BG_VRAM + TILE_OFFSET_4BPP(sState->tileIds[i])),
                  TILE_SIZE_4BPP);
    sState->tileCopyCount = 0;

    for (u32 layer = 0; layer < 3; layer++)
    {
        u32 bg = layer + 1;
        if (GetTilemap(layer) && GetBgTilemapBuffer(bg) == GetTilemap(layer))
            DmaCopy16(3, GetTilemap(layer),
                      (void *)BG_SCREEN_ADDR(GetBgAttribute(bg, BG_ATTR_MAPBASEINDEX)),
                      BG_SCREEN_SIZE);
    }

    TransferPlttBuffer();
    sState->ready = FALSE;
    sTilemapsCommitted = sTilemapsPrepared;
}

u32 OpalMapPalettesTilemapsPrepared(void)
{
    return sTilemapsPrepared;
}

u32 OpalMapPalettesTilemapsCommitted(void)
{
    return sTilemapsCommitted;
}

const u8 *OpalMapPalettesGetTileGraphics(u32 tile)
{
    if (sState)
        for (u32 i = 0; i < sState->tileCopyCount; i++)
            if (sState->tileIds[i] == tile)
                return sState->tileSources[i];
    return (const u8 *)(BG_VRAM + TILE_OFFSET_4BPP(tile));
}

bool32 OpalMapPalettesQueueTile(const void *source, u32 tile)
{
    if (!sState)
        return FALSE;

    for (u32 i = 0; i < sState->tileCopyCount; i++)
    {
        if (sState->tileIds[i] == tile)
        {
            sState->tileSources[i] = source;
            return TRUE;
        }
    }

    REQUIRE_PALETTE(sState->tileCopyCount < ARRAY_COUNT(sState->tileSources),
                    "Map tile transaction overflow");

    u32 index = sState->tileCopyCount++;
    sState->tileSources[index] = source;
    sState->tileIds[index] = tile;
    sState->dirty = TRUE;
    return TRUE;
}

void OpalMapPalettesPin(u32 logical)
{
    if (!sState)
        return;

    REQUIRE_PALETTE(logical < OPAL_MAP_PHYSICAL_BANKS
                 && (sState->pinned & (1u << logical)),
                    "Undeclared pinned map palette %u", logical);
}

void OpalMapPalettesReserve(u32 banks)
{
    if (!sState)
        return;

    // UI BG palette banks 13..15 are not controlled by M3, so callers may reserve them
    // without affecting the map allocator.
    banks &= OPAL_MAP_PHYSICAL_MASK;
    if (!banks)
        return;

    REQUIRE_PALETTE(!(banks & ~sState->reserved),
                    "Undeclared map palette reservation %x", banks);
}

u16 OpalMapPalettesResolve(u16 tile, u32 logical)
{
    if (!sState)
        return tile;

    if (sState->dirty)
        Prepare();

    REQUIRE_PALETTE(logical < OPAL_MAP_PALETTES_COUNT
                 && sState->banks[logical] != OPAL_MAP_PALETTE_NONE,
                    "Unresident map palette %u", logical);

    return (tile & 0x0FFF) | (sState->banks[logical] << 12);
}
