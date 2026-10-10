#include "global.h"
#include "map_palette.h"
#include "bg.h"
#include "field_weather.h"
#include "malloc.h"
#include "overworld.h"
#include "palette.h"
#include "constants/field_weather.h"

struct MapPaletteState
{
    const struct MapLayout *layout;
    const struct TilesetPaletteExtension *extension;
    u8 references[3][MAP_PALETTE_CELLS];
    u8 banks[MAP_PALETTES_COUNT];
    u16 useCounts[MAP_PALETTES_COUNT];
    u8 owners[NUM_PALS_TOTAL];
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

static EWRAM_DATA struct MapPaletteState *sState = NULL;
// Running totals of field tilemap states made ready for VBlank and of those transferred, so other code can tell when
// a tilemap change has reached VRAM while this module owns the field tilemap copies.
static u32 sTilemapsPrepared;
static u32 sTilemapsCommitted;
static volatile bool8 sInFrame;

#define REQUIRE_PALETTE(condition, ...) do { \
    if (!(condition)) { \
        MapPalettesReset(); \
        fatalf(__VA_ARGS__); \
    } \
} while (0)

bool32 MapPaletteAllocate(u32 required, u32 reserved, u32 pinned, const u8 *previous, u8 *result)
{
    u32 occupied = reserved;
    required |= pinned;
    if ((required >> MAP_PALETTES_COUNT) || (reserved >> NUM_PALS_TOTAL)
     || (pinned >> NUM_PALS_TOTAL) || (reserved & pinned))
        return FALSE;

    memset(result, MAP_PALETTE_NONE, MAP_PALETTES_COUNT);
    for (u32 logical = 0; logical < NUM_PALS_TOTAL; logical++)
    {
        if (pinned & (1u << logical))
        {
            result[logical] = logical;
            occupied |= 1u << logical;
        }
    }
    for (u32 logical = 0; logical < MAP_PALETTES_COUNT; logical++)
    {
        u32 bank = previous[logical];
        if ((required & (1u << logical)) && result[logical] == MAP_PALETTE_NONE
         && bank < NUM_PALS_TOTAL && !(occupied & (1u << bank)))
        {
            result[logical] = bank;
            occupied |= 1u << bank;
        }
    }
    for (u32 logical = 0; logical < MAP_PALETTES_COUNT; logical++)
    {
        if (!(required & (1u << logical)) || result[logical] != MAP_PALETTE_NONE)
            continue;
        u32 bank;
        for (bank = 0; bank < NUM_PALS_TOTAL && (occupied & (1u << bank)); bank++);
        if (bank == NUM_PALS_TOTAL)
            return FALSE;
        result[logical] = bank;
        occupied |= 1u << bank;
    }
    return TRUE;
}

bool32 MapPalettesActive(void)
{
    return sState != NULL;
}

// Whether a connected map's palettes can take over the current allocation as it is: the same library on
// the same primary tileset. The cells of the previous map then keep valid references.
bool32 MapPalettesCanContinue(const struct MapLayout *layout)
{
    return sState && sState->extension->tileset == layout->secondaryTileset
        && sState->layout->primaryTileset == layout->primaryTileset;
}

u32 MapPalettesMetatileCount(const struct Tileset *tileset, u32 fallback)
{
    if (!sState)
        return fallback;
    if (tileset == sState->extension->tileset)
        return sState->extension->metatileCount;
    if (tileset == sState->layout->primaryTileset)
        return sState->primaryMetatileCount;
    return fallback;
}

void MapPalettesOnHeapReset(void)
{
    // Save-block relocation may already have overwritten the heap.
    sState = NULL;
    sTilemapsCommitted = sTilemapsPrepared;
}

void MapPalettesReset(void)
{
    if (sState)
        Free(sState);
    sState = NULL;
    sTilemapsCommitted = sTilemapsPrepared;
}

void MapPalettesLoad(const struct MapLayout *layout)
{
    const struct TilesetPaletteExtension *extension = gTilesetPaletteExtensions;
    const struct MapPaletteProfile *profile = gMapPaletteProfiles;

    while (extension->tileset && extension->tileset != layout->secondaryTileset)
        extension++;
    if (!extension->tileset)
    {
        MapPalettesReset();
        return;
    }
    while (profile->layout && profile->layout != layout)
        profile++;
    REQUIRE_PALETTE(profile->layout, "Extended tileset used by an uncertified layout");

    MapPalettesLoadProfile(layout, extension, profile);
}

void MapPalettesLoadProfile(const struct MapLayout *layout, const struct TilesetPaletteExtension *extension,
                               const struct MapPaletteProfile *profile)
{
    // Seamless connections are certified to use exactly the same tileset pair.
    if (MapPalettesCanContinue(layout))
    {
        sState->layout = layout;
        sState->reserved |= profile->reservedBanks;
        sState->pinned |= profile->pinnedPalettes;
        sState->resident |= profile->residentPalettes;
        sState->dirty = TRUE;
        return;
    }
    MapPalettesReset();
    sState = AllocZeroed(sizeof(*sState));
    REQUIRE_PALETTE(sState, "Cannot allocate map palette state");
    sState->layout = layout;
    sState->extension = extension;
    sState->reserved = profile->reservedBanks;
    sState->pinned = profile->pinnedPalettes | 1;
    sState->resident = profile->residentPalettes;
    sState->primaryMetatileCount = profile->primaryMetatileCount;
    memset(sState->references, MAP_PALETTE_NONE, sizeof(sState->references));
    memset(sState->banks, MAP_PALETTE_NONE, sizeof(sState->banks));
    memset(sState->owners, MAP_PALETTE_NONE, sizeof(sState->owners));
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

const u8 *MapPalettesGetReferences(const struct Tileset *tileset, u32 metatile)
{
    if (!sState || tileset != sState->extension->tileset)
        return NULL;
    REQUIRE_PALETTE(metatile < sState->extension->metatileCount, "Invalid extended metatile %u", metatile);
    return sState->extension->references + metatile * NUM_TILES_PER_METATILE;
}

static void SetReference(u32 layer, u32 cell, u32 logical)
{
    REQUIRE_PALETTE(cell < MAP_PALETTE_CELLS && logical < MAP_PALETTES_COUNT,
                    "Invalid map palette reference %u at %u", logical, cell);
    u32 old = sState->references[layer][cell];
    if (old != logical)
    {
        if (old != MAP_PALETTE_NONE)
            sState->useCounts[old]--;
        sState->useCounts[logical]++;
        sState->references[layer][cell] = logical;
    }
    u16 *tilemap = GetTilemap(layer);
    u32 bank = sState->banks[logical];
    if (tilemap)
        tilemap[cell] = (tilemap[cell] & 0xFFF) | ((bank == MAP_PALETTE_NONE ? 0 : bank) << 12);
}

void MapPalettesSetMetatile(u32 offset, u32 layerType, const u16 *tiles, const u8 *references)
{
    static const u8 offsets[] = {0, 1, 32, 33};
    if (!sState)
        return;
    for (u32 layer = 0; layer < 3; layer++)
    {
        for (u32 tile = 0; tile < 4; tile++)
        {
            u32 source = (2 - layer) * 4 + tile;
            u32 logical;
            if (layerType == 0xFF)
            {
                if (layer == 1)
                {
                    SetReference(layer, offset + offsets[tile], 0);
                    continue;
                }
                source = (layer == 0 ? 4 : 0) + tile;
            }
            logical = references ? references[source] : tiles[source] >> 12;
            SetReference(layer, offset + offsets[tile], logical);
        }
    }
    sState->dirty = TRUE;
}

static const u16 *GetBasePalette(u32 logical)
{
    if (logical >= NUM_PALS_TOTAL)
        return sState->extension->extraPalettes[logical - NUM_PALS_TOTAL];
    const struct Tileset *tileset = logical < GetNumPalsInPrimary(sState->layout)
                                 ? sState->layout->primaryTileset : sState->layout->secondaryTileset;
    return tileset->palettes[logical];
}

void MapPalettesUpdateAlternates(u32 banks)
{
    if (!sState || !MapHasNaturalLight(gMapHeader.mapType))
        return;
    for (u32 bank = 1; bank < NUM_PALS_TOTAL; bank++)
    {
        u32 logical = sState->owners[bank];
        const u16 *night = NULL;
        if (!(banks & (1u << bank)) || logical == MAP_PALETTE_NONE)
            continue;
        if (logical >= NUM_PALS_TOTAL)
            night = sState->extension->extraNightPalettes[logical - NUM_PALS_TOTAL];
        else
        {
            u32 primaryCount = GetNumPalsInPrimary(sState->layout);
            const struct Tileset *tileset = logical < primaryCount
                                         ? sState->layout->primaryTileset : sState->layout->secondaryTileset;
            u32 bit = logical < primaryCount ? logical : logical - primaryCount;
            if (tileset->swapPalettes & (1u << bit))
                night = tileset->palettes[(logical + 9) % 16];
        }
        if (night)
            AvgPaletteWeighted((u16 *)GetBasePalette(logical), (u16 *)night,
                               &gPlttBufferUnfaded[BG_PLTT_ID(bank)], gTimeBlend.altWeight);
    }
}

u32 MapPalettesMask(void)
{
    return sState ? PALETTES_MAP & ~sState->reserved : PALETTES_MAP;
}

u32 MapPalettesWeatherType(u32 bank, u32 fallback)
{
    if (!sState || bank >= NUM_PALS_TOTAL)
        return fallback;
    u32 logical = sState->owners[bank];
    if ((sState->reserved & (1u << bank)) || logical == MAP_PALETTE_NONE)
        return COLOR_MAP_NONE;
    if (sState->pinned & (1u << bank))
        return fallback;
    if (logical >= NUM_PALS_TOTAL)
        return sState->extension->extraWeatherTypes[logical - NUM_PALS_TOTAL];
    return COLOR_MAP_DARK_CONTRAST;
}

static void Prepare(void)
{
    u8 next[MAP_PALETTES_COUNT];
    u16 ALIGNED(4) unfaded[NUM_PALS_TOTAL][16];
    u16 ALIGNED(4) faded[NUM_PALS_TOTAL][16];
    u32 required = sState->pinned | sState->resident;
    u32 reserved = sState->reserved;
    u32 pinned = sState->pinned;

    for (u32 logical = 0; logical < MAP_PALETTES_COUNT; logical++)
        if (sState->useCounts[logical])
            required |= 1u << logical;
    REQUIRE_PALETTE(MapPaletteAllocate(required, sState->reserved, sState->pinned, sState->banks, next),
                  "Map palette overflow: need %x reserved %x pinned %x", required, reserved, pinned);

    if (!memcmp(next, sState->banks, sizeof(next)))
    {
        sState->dirty = FALSE;
        sState->ready = TRUE;
        sTilemapsPrepared++;
        return;
    }
    CpuFastCopy(gPlttBufferUnfaded, unfaded, sizeof(unfaded));
    CpuFastCopy(gPlttBufferFaded, faded, sizeof(faded));
    memset(sState->owners, MAP_PALETTE_NONE, sizeof(sState->owners));
    for (u32 logical = 0; logical < MAP_PALETTES_COUNT; logical++)
        if (next[logical] != MAP_PALETTE_NONE)
            sState->owners[next[logical]] = logical;

    for (u32 logical = 0; logical < MAP_PALETTES_COUNT; logical++)
    {
        u32 bank = next[logical];
        u32 old = sState->banks[logical];
        if (bank == MAP_PALETTE_NONE || bank == old)
            continue;
        if (old != MAP_PALETTE_NONE)
        {
            CpuCopy16(unfaded[old], &gPlttBufferUnfaded[BG_PLTT_ID(bank)], PLTT_SIZE_4BPP);
            CpuCopy16(faded[old], &gPlttBufferFaded[BG_PLTT_ID(bank)], PLTT_SIZE_4BPP);
        }
        else
        {
            LoadPalette(GetBasePalette(logical), BG_PLTT_ID(bank), PLTT_SIZE_4BPP);
            if (!logical)
                gPlttBufferUnfaded[0] = gPlttBufferFaded[0] = 0;
            MapPalettesUpdateAlternates(1u << bank);
            ApplyWeatherToNewMapPalette(bank);
        }
    }
    memcpy(sState->banks, next, sizeof(next));
    for (u32 layer = 0; layer < 3; layer++)
    {
        u16 *tilemap = GetTilemap(layer);
        if (!tilemap)
            continue;
        for (u32 cell = 0; cell < MAP_PALETTE_CELLS; cell++)
        {
            u32 logical = sState->references[layer][cell];
            if (logical != MAP_PALETTE_NONE)
                tilemap[cell] = (tilemap[cell] & 0xFFF) | (next[logical] << 12);
        }
    }
    sState->dirty = FALSE;
    sState->ready = TRUE;
    sTilemapsPrepared++;
}

void MapPalettesBeginFrame(void)
{
    sInFrame = TRUE;
}

void MapPalettesEndFrame(void)
{
    if (sState && sState->dirty)
        Prepare();
    sInFrame = FALSE;
}

bool32 MapPalettesBlockTransfer(void)
{
    // Admit a transaction only near the start of VBlank (68 lines total).
    // A missed deadline repeats the previous complete frame, including scroll/OAM.
    return sState && (sInFrame || REG_VCOUNT > 164
                  || (sState->ready && gPaletteFade.bufferTransferDisabled));
}

bool32 MapPalettesQueueBg(u32 bg)
{
    if (!sState || bg < 1 || bg > 3 || GetBgTilemapBuffer(bg) != GetTilemap(bg - 1))
        return FALSE;
    sState->dirty = TRUE;
    return TRUE;
}

void MapPalettesCommit(void)
{
    if (!sState || !sState->ready)
        return;
    for (u32 i = 0; i < sState->tileCopyCount; i++)
        DmaCopy16(3, sState->tileSources[i], (void *)(BG_VRAM + TILE_OFFSET_4BPP(sState->tileIds[i])), TILE_SIZE_4BPP);
    sState->tileCopyCount = 0;
    for (u32 layer = 0; layer < 3; layer++)
    {
        u32 bg = layer + 1;
        if (GetTilemap(layer) && GetBgTilemapBuffer(bg) == GetTilemap(layer))
            DmaCopy16(3, GetTilemap(layer), (void *)BG_SCREEN_ADDR(GetBgAttribute(bg, BG_ATTR_MAPBASEINDEX)), BG_SCREEN_SIZE);
    }
    TransferPlttBuffer();
    sState->ready = FALSE;
    sTilemapsCommitted = sTilemapsPrepared;
}

u32 MapPalettesTilemapsPrepared(void)
{
    return sTilemapsPrepared;
}

u32 MapPalettesTilemapsCommitted(void)
{
    return sTilemapsCommitted;
}

// The pixels a map tile slot shows once the pending transaction is published.
const u8 *MapPalettesGetTileGraphics(u32 tile)
{
    if (sState)
    {
        for (u32 i = 0; i < sState->tileCopyCount; i++)
        {
            if (sState->tileIds[i] == tile)
                return sState->tileSources[i];
        }
    }
    return (const u8 *)(BG_VRAM + TILE_OFFSET_4BPP(tile));
}

bool32 MapPalettesQueueTile(const void *source, u32 tile)
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
    REQUIRE_PALETTE(sState->tileCopyCount < ARRAY_COUNT(sState->tileSources), "Map tile transaction overflow");
    u32 index = sState->tileCopyCount++;
    sState->tileSources[index] = source;
    sState->tileIds[index] = tile;
    sState->dirty = TRUE;
    return TRUE;
}

void MapPalettesPin(u32 logical)
{
    if (!sState)
        return;
    REQUIRE_PALETTE(logical < NUM_PALS_TOTAL && (sState->pinned & (1u << logical)),
                    "Undeclared pinned map palette %u", logical);
}

void MapPalettesReserve(u32 banks)
{
    if (!sState)
        return;
    REQUIRE_PALETTE(!(banks & PALETTES_MAP & ~sState->reserved),
                    "Undeclared map palette reservation %x", banks);
}

u16 MapPalettesResolve(u16 tile, u32 logical)
{
    if (!sState)
        return tile;
    if (sState->dirty)
        Prepare();
    REQUIRE_PALETTE(logical < MAP_PALETTES_COUNT && sState->banks[logical] != MAP_PALETTE_NONE,
                  "Unresident map palette %u", logical);
    return (tile & 0xFFF) | (sState->banks[logical] << 12);
}
