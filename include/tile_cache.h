#ifndef GUARD_TILE_CACHE_H
#define GUARD_TILE_CACHE_H

#include "fieldmap.h"

/*
 * Streamed map tiles.
 *
 * All three map backgrounds share one 1024-tile character window, so at most 1024 map tiles can be
 * in VRAM at once. Tilesets may still hold more tiles than that: a map whose tilesets are
 * "streamed" keeps the primary tileset's first 512 tiles resident as usual, and loads every other
 * tile it draws into a slot of the tile cache on demand, releasing slots once no tilemap cell
 * references them.
 *
 * Metatile tile words keep the hardware layout, whose tile id has 10 bits. A tileset with a
 * metatile_tiles_ext.bin supplies tile id bits 10-11 for each of its metatile sub-tiles, giving
 * virtual tile ids that keep every existing (10 bit) reference meaning what it always did:
 *
 *   [   0,  512) primary tiles 0-511
 *   [ 512, 1024) secondary tiles 0-511
 *   [1024, 1536) primary tiles 512-1023
 *   [1536, 3072) secondary tiles 512-2047
 *
 * Animated tiles keep the VRAM slots tileset_anims.c writes them to, so a streamed map pins those
 * slots to the tiles they belong to.
 */

#define NUM_TILES_IN_PRIMARY_EXTENDED   1024
#define NUM_TILES_IN_SECONDARY_EXTENDED 2048
#define NUM_VIRTUAL_TILES (NUM_TILES_IN_PRIMARY_EXTENDED + NUM_TILES_IN_SECONDARY_EXTENDED)

#define TILEMAP_TILE_NUM_MASK 0x3FF

#define METATILE_EXT_TILE_HIGH_SHIFT 5
#define METATILE_EXT_TILE_HIGH_MASK  (3 << METATILE_EXT_TILE_HIGH_SHIFT)

// shop.c overwrites the last 29 map tile slots with its menu graphics, and doors use the last 16.
#define TILE_CACHE_FIRST_SLOT NUM_TILES_IN_PRIMARY
#define TILE_CACHE_END_SLOT   995
#define TILE_CACHE_NUM_SLOTS  (TILE_CACHE_END_SLOT - TILE_CACHE_FIRST_SLOT)
#define TILE_CACHE_DOOR_SLOTS_START (NUM_TILES_TOTAL - 16)

struct TilesetCapacityInfo
{
    const struct Tileset *tileset;
    const u32 *rawTiles;
    const u8 *tileExt;
    u16 numTiles;
    u16 numMetatiles;
    bool8 streamed;
};

extern const struct TilesetCapacityInfo gTilesetCapacityInfo[];
extern const u32 gTilesetCapacityInfoCount;

const struct TilesetCapacityInfo *GetTilesetCapacityInfo(const struct Tileset *tileset);
u32 GetTilesetNumMetatiles(const struct Tileset *tileset, u32 fallback);
bool32 TileCache_LayoutIsStreamed(const struct MapLayout *layout);
bool32 TileCache_IsActive(void);
void TileCache_LoadLayout(const struct MapLayout *layout);
void TileCache_SwitchLayout(const struct MapLayout *layout);
bool32 TileCache_Disable(void);
void TileCache_RequestRedraw(void);
bool32 TileCache_TakeRedrawRequest(void);
const u8 *TileCache_GetMetatileExt(const struct Tileset *tileset, u32 metatile);
void TileCache_WriteCell(u16 *cell, u16 tile, u32 ext);
void TileCache_WriteCellPhysical(u16 *cell, u16 tile);
u16 TileCache_Resolve(u16 tile, u32 ext);
bool32 TileCache_AllowsAnimDest(u32 firstSlot, u32 numSlots);
u32 TileCache_GetOverflowCount(void);

#endif // GUARD_TILE_CACHE_H
