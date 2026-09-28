#ifndef GUARD_OPAL_MAP_PALETTE_H
#define GUARD_OPAL_MAP_PALETTE_H

#include "fieldmap.h"

/*
 * Kyoto/Opal M3 logical palette model.
 *
 * Logical palettes:
 *   0..13  - authored from the normal primary/secondary tileset palette tables.
 *   14..17 - four additional palettes stored in the Opal palette sidecar.
 *
 * Physical map palette banks:
 *   0..12  - the 13 map banks already used by pokeemerald-expansion/Kyoto.
 *   13..15 - remain outside M3 and stay available to UI/preview/window code.
 *
 * M3 never increases the GBA's physical palette memory. It remaps only the
 * palette nibble in BG1/BG2/BG3 tilemap entries as the camera view changes.
 */
#define OPAL_MAP_PALETTES_COUNT 18
#define OPAL_MAP_BASE_LOGICAL_PALETTES 14
#define OPAL_MAP_EXTRA_PALETTES 4
#define OPAL_MAP_PALETTE_NONE 0xFF
#define OPAL_MAP_PALETTE_CELLS (32 * 32)
#define OPAL_MAP_PHYSICAL_BANKS NUM_PALS_TOTAL
#define OPAL_MAP_PHYSICAL_MASK ((1u << OPAL_MAP_PHYSICAL_BANKS) - 1)

struct OpalTilesetPaletteExtension
{
    const struct Tileset *tileset;
    const u16 (*extraPalettes)[16];
    const u16 *const *extraNightPalettes;
    const u8 *references;
    u16 metatileCount;
    u8 extraWeatherTypes[OPAL_MAP_EXTRA_PALETTES];
};

struct OpalMapPaletteProfile
{
    const struct MapLayout *layout;
    u16 reservedBanks;
    u16 pinnedPalettes;
    u32 residentPalettes;
    u16 primaryMetatileCount;
};

extern const struct OpalTilesetPaletteExtension gOpalTilesetPaletteExtensions[];
extern const struct OpalMapPaletteProfile gOpalMapPaletteProfiles[];

bool32 OpalMapPalettesActive(void);
u32 OpalMapPalettesMetatileCount(const struct Tileset *tileset, u32 fallback);
void OpalMapPalettesReset(void);
void OpalMapPalettesOnHeapReset(void);
void OpalMapPalettesLoad(const struct MapLayout *layout);
void OpalMapPalettesLoadProfile(const struct MapLayout *layout, const struct OpalTilesetPaletteExtension *extension, const struct OpalMapPaletteProfile *profile);
void OpalMapPalettesBeginFrame(void);
void OpalMapPalettesEndFrame(void);
bool32 OpalMapPalettesBlockTransfer(void);
void OpalMapPalettesCommit(void);
bool32 OpalMapPalettesQueueBg(u32 bg);
u32 OpalMapPalettesTilemapsPrepared(void);
u32 OpalMapPalettesTilemapsCommitted(void);
bool32 OpalMapPalettesQueueTile(const void *source, u32 tile);
void OpalMapPalettesSetMetatile(u32 offset, u32 layerType, const u16 *tiles, const u8 *references, const u16 *thirdTiles);
const u8 *OpalMapPalettesGetReferences(const struct Tileset *tileset, u32 metatile);
u32 OpalMapPalettesWeatherType(u32 bank, u32 fallback);
u32 OpalMapPalettesMask(void);
void OpalMapPalettesUpdateAlternates(u32 banks);
void OpalMapPalettesPin(u32 logical);
void OpalMapPalettesReserve(u32 banks);
u16 OpalMapPalettesResolve(u16 tile, u32 logical);
bool32 OpalMapPaletteAllocate(u32 required, u32 reserved, u32 pinned, const u8 *previous, u8 *result);

#endif
