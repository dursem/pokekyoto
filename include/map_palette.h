#ifndef GUARD_MAP_PALETTE_H
#define GUARD_MAP_PALETTE_H

#include "fieldmap.h"

#define MAP_PALETTES_COUNT (NUM_PALS_TOTAL + 4)
#define MAP_PALETTE_NONE 0xFF
#define MAP_PALETTE_CELLS (32 * 32)

struct TilesetPaletteExtension
{
    const struct Tileset *tileset;
    const u16 (*extraPalettes)[16];
    const u16 *const *extraNightPalettes;
    const u8 *references;
    u16 metatileCount;
    u8 extraWeatherTypes[4];
};

struct MapPaletteProfile
{
    const struct MapLayout *layout;
    u16 reservedBanks;
    u16 pinnedPalettes;
    u32 residentPalettes;
    u16 primaryMetatileCount;
};

extern const struct TilesetPaletteExtension gTilesetPaletteExtensions[];
extern const struct MapPaletteProfile gMapPaletteProfiles[];

bool32 MapPalettesActive(void);
bool32 MapPalettesCanContinue(const struct MapLayout *layout);
u32 MapPalettesMetatileCount(const struct Tileset *tileset, u32 fallback);
void MapPalettesReset(void);
void MapPalettesOnHeapReset(void);
void MapPalettesLoad(const struct MapLayout *layout);
void MapPalettesLoadProfile(const struct MapLayout *layout, const struct TilesetPaletteExtension *extension, const struct MapPaletteProfile *profile);
void MapPalettesBeginFrame(void);
void MapPalettesEndFrame(void);
bool32 MapPalettesBlockTransfer(void);
void MapPalettesCommit(void);
bool32 MapPalettesQueueBg(u32 bg);
u32 MapPalettesTilemapsPrepared(void);
u32 MapPalettesTilemapsCommitted(void);
bool32 MapPalettesQueueTile(const void *source, u32 tile);
const u8 *MapPalettesGetTileGraphics(u32 tile);
void MapPalettesSetMetatile(u32 offset, u32 layerType, const u16 *tiles, const u8 *references);
const u8 *MapPalettesGetReferences(const struct Tileset *tileset, u32 metatile);
u32 MapPalettesWeatherType(u32 bank, u32 fallback);
u32 MapPalettesMask(void);
void MapPalettesUpdateAlternates(u32 banks);
void MapPalettesPin(u32 logical);
void MapPalettesReserve(u32 banks);
u16 MapPalettesResolve(u16 tile, u32 logical);
bool32 MapPaletteAllocate(u32 required, u32 reserved, u32 pinned, const u8 *previous, u8 *result);

#endif
