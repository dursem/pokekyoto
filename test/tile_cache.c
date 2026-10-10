#include "global.h"
#include "bg.h"
#include "dma3.h"
#include "field_camera.h"
#include "field_weather.h"
#include "fieldmap.h"
#include "main.h"
#include "malloc.h"
#include "map_palette.h"
#include "palette.h"
#include "sprite.h"
#include "task.h"
#include "overworld.h"
#include "tile_cache.h"
#include "test/test.h"
#include "constants/map_types.h"

// The TileCacheTest tilesets are built by tools/tilesetcap/make_test_tilesets.py from General and Fortree: each
// source metatile is followed by a twin that draws the same image from mirrored copies of its tiles in the
// extended range.
#define TEST_PRIMARY_METATILES   512
#define TEST_PRIMARY_TWINS       160
#define TEST_SECONDARY_METATILES 280
#define TEST_SECONDARY_TWINS     280
#define TEST_ANIM_FIRST_SLOT     TILE_CACHE_FIRST_SLOT
#define TEST_ANIM_SLOTS          8

extern const struct MapLayout TileCacheTest_Layout;
extern const struct MapLayout Route120_Layout;

static u32 sDummyDmaTarget[4];

static void BeginLayout(const struct MapLayout *layout, bool32 streamed)
{
    REG_IME = 0;
    gMain.vblankCounter1 = 1;
    gMapHeader.mapLayout = layout;
    gOverworldTilemapBuffer_Bg1 = AllocZeroed(BG_SCREEN_SIZE);
    gOverworldTilemapBuffer_Bg2 = AllocZeroed(BG_SCREEN_SIZE);
    gOverworldTilemapBuffer_Bg3 = AllocZeroed(BG_SCREEN_SIZE);
    CpuFastCopy(GetTilesetCapacityInfo(layout->primaryTileset)->rawTiles, (void *)BG_VRAM, NUM_TILES_IN_PRIMARY * TILE_SIZE_4BPP);
    if (streamed)
        TileCache_LoadLayout(layout);
    else
        TileCache_Disable();
}

static void EndLayout(void)
{
    TileCache_Disable();
    ClearDma3Requests();
    TRY_FREE_AND_SET_NULL(gOverworldTilemapBuffer_Bg1);
    TRY_FREE_AND_SET_NULL(gOverworldTilemapBuffer_Bg2);
    TRY_FREE_AND_SET_NULL(gOverworldTilemapBuffer_Bg3);
    REG_IME = 1;
}

static u32 TilePixel(const u8 *tile, u16 word, u32 x, u32 y)
{
    if (word & (1 << 10))
        x = 7 - x;
    if (word & (1 << 11))
        y = 7 - y;
    return (x & 1) ? tile[y * 4 + x / 2] >> 4 : tile[y * 4 + x / 2] & 0xF;
}

static void RenderCell(u16 cell, u8 *pixels)
{
    const u8 *tile = (const u8 *)(BG_VRAM + TILE_OFFSET_4BPP(cell & TILEMAP_TILE_NUM_MASK));
    for (u32 i = 0; i < 64; i++)
        pixels[i] = TilePixel(tile, cell, i % 8, i / 8);
}

// What a pre-streaming tile word shows, straight from the tilesets' tile data.
static void RenderLegacyWord(const struct MapLayout *layout, u16 word, u8 *pixels)
{
    u32 tile = word & TILEMAP_TILE_NUM_MASK;
    const struct TilesetCapacityInfo *info = GetTilesetCapacityInfo(tile < NUM_TILES_IN_PRIMARY ? layout->primaryTileset : layout->secondaryTileset);
    const u8 *src = (const u8 *)info->rawTiles + TILE_OFFSET_4BPP(tile % NUM_TILES_IN_PRIMARY);
    for (u32 i = 0; i < 64; i++)
        pixels[i] = TilePixel(src, word, i % 8, i / 8);
}

static void WriteVirtualTile(u16 *cell, u32 tile)
{
    TileCache_WriteCell(cell, tile & TILEMAP_TILE_NUM_MASK, (tile >> 10) << METATILE_EXT_TILE_HIGH_SHIFT);
}

TEST("Tile cache: Only layouts using a tileset with extended tiles stream them")
{
    EXPECT(TileCache_LayoutIsStreamed(&TileCacheTest_Layout));
    EXPECT(!TileCache_LayoutIsStreamed(&Route120_Layout));
}

TEST("Tile cache: Streamed metatiles show the same pixels as the tiles they reference")
{
    const struct Tileset *tilesets[] = {TileCacheTest_Layout.primaryTileset, TileCacheTest_Layout.secondaryTileset};
    const u32 counts[] = {TEST_PRIMARY_METATILES, TEST_SECONDARY_METATILES};
    const u32 twins[] = {TEST_PRIMARY_TWINS, TEST_SECONDARY_TWINS};
    u8 expected[64], actual[64];
    u32 mismatches = 0;

    BeginLayout(&TileCacheTest_Layout, TRUE);
    for (u32 t = 0; t < ARRAY_COUNT(tilesets); t++)
    {
        const u16 *metatiles = tilesets[t]->metatiles;
        const u8 *ext = GetTilesetCapacityInfo(tilesets[t])->tileExt;

        for (u32 metatile = 0; metatile < twins[t]; metatile++)
        {
            for (u32 k = 0; k < NUM_TILES_PER_METATILE; k++)
            {
                u32 source = metatile * NUM_TILES_PER_METATILE + k;
                u32 twin = (counts[t] + metatile) * NUM_TILES_PER_METATILE + k;

                RenderLegacyWord(&TileCacheTest_Layout, metatiles[source], expected);
                TileCache_WriteCell(&gOverworldTilemapBuffer_Bg3[0], metatiles[source], ext[source]);
                RenderCell(gOverworldTilemapBuffer_Bg3[0], actual);
                mismatches += memcmp(expected, actual, sizeof(actual)) != 0;

                TileCache_WriteCell(&gOverworldTilemapBuffer_Bg3[1], metatiles[twin], ext[twin]);
                RenderCell(gOverworldTilemapBuffer_Bg3[1], actual);
                mismatches += memcmp(expected, actual, sizeof(actual)) != 0;
                gMain.vblankCounter1++;
            }
        }
    }
    EXPECT_EQ(mismatches, 0);
    EXPECT_EQ(TileCache_GetOverflowCount(), 0);
    EndLayout();
}

TEST("Tile cache: Animated tiles of a streamed map keep pinned slots and other animation targets are refused")
{
    u8 expected[64], actual[64];

    BeginLayout(&TileCacheTest_Layout, TRUE);
    EXPECT(TileCache_AllowsAnimDest(TEST_ANIM_FIRST_SLOT, TEST_ANIM_SLOTS));
    EXPECT(TileCache_AllowsAnimDest(0, NUM_TILES_IN_PRIMARY));
    EXPECT(!TileCache_AllowsAnimDest(TEST_ANIM_FIRST_SLOT + TEST_ANIM_SLOTS, 1));
    // Stock animations into the slots past the pool (shared with doors and the shop) keep writing them directly.
    EXPECT(TileCache_AllowsAnimDest(TILE_CACHE_END_SLOT, NUM_TILES_TOTAL - TILE_CACHE_END_SLOT));

    // A pinned tile is found where the animation writes it.
    TileCache_WriteCell(&gOverworldTilemapBuffer_Bg3[0], TEST_ANIM_FIRST_SLOT, 0);
    EXPECT_EQ(gOverworldTilemapBuffer_Bg3[0] & TILEMAP_TILE_NUM_MASK, TEST_ANIM_FIRST_SLOT);
    RenderLegacyWord(&TileCacheTest_Layout, TEST_ANIM_FIRST_SLOT, expected);
    RenderCell(gOverworldTilemapBuffer_Bg3[0], actual);
    EXPECT_EQ(memcmp(expected, actual, sizeof(actual)), 0);
    EndLayout();
}

TEST("Tile cache: A released slot is not reused until the tilemap copy that released it has been transferred")
{
    u16 *cells;
    u32 overflows, filled, releasedSlot, tile = NUM_TILES_TOTAL;

    BeginLayout(&TileCacheTest_Layout, TRUE);
    cells = gOverworldTilemapBuffer_Bg3;
    overflows = TileCache_GetOverflowCount();
    for (filled = 0; TileCache_GetOverflowCount() == overflows; filled++)
        WriteVirtualTile(&cells[filled], tile++);
    EXPECT_GT(filled, TILE_CACHE_NUM_SLOTS - 64);
    EXPECT_EQ(cells[filled - 1] & TILEMAP_TILE_NUM_MASK, 0);

    releasedSlot = cells[0] & TILEMAP_TILE_NUM_MASK;
    RequestDma3Fill(0, sDummyDmaTarget, sizeof(sDummyDmaTarget), 1);
    TileCache_WriteCell(&cells[0], 0, 0);
    gMain.vblankCounter1++;

    WriteVirtualTile(&cells[filled], tile++);
    EXPECT_EQ(cells[filled] & TILEMAP_TILE_NUM_MASK, 0);
    EXPECT_EQ(TileCache_GetOverflowCount(), overflows + 2);

    ClearDma3Requests();
    gMain.vblankCounter1++;
    WriteVirtualTile(&cells[filled + 1], tile++);
    EXPECT_EQ(cells[filled + 1] & TILEMAP_TILE_NUM_MASK, releasedSlot);
    EndLayout();
}

TEST("Tile cache: Rewriting a cell with the tile it already shows keeps its slot")
{
    u32 slot;

    BeginLayout(&TileCacheTest_Layout, TRUE);
    WriteVirtualTile(&gOverworldTilemapBuffer_Bg3[0], NUM_TILES_TOTAL + 5);
    slot = gOverworldTilemapBuffer_Bg3[0] & TILEMAP_TILE_NUM_MASK;
    EXPECT(slot >= TILE_CACHE_FIRST_SLOT && slot < TILE_CACHE_END_SLOT);
    WriteVirtualTile(&gOverworldTilemapBuffer_Bg3[0], NUM_TILES_TOTAL + 5);
    EXPECT_EQ(gOverworldTilemapBuffer_Bg3[0] & TILEMAP_TILE_NUM_MASK, slot);

    // Released and needed again before anything else took the slot: still loaded, so it comes back.
    TileCache_WriteCell(&gOverworldTilemapBuffer_Bg3[0], 0, 0);
    gMain.vblankCounter1++;
    WriteVirtualTile(&gOverworldTilemapBuffer_Bg3[1], NUM_TILES_TOTAL + 5);
    EXPECT_EQ(gOverworldTilemapBuffer_Bg3[1] & TILEMAP_TILE_NUM_MASK, slot);
    EndLayout();
}

TEST("Tile cache: Walking into a streamed map keeps the slots the previous map's cells still show")
{
    const u32 legacyCells = 100;
    u16 *cells;
    u32 overflows, tile = NUM_TILES_TOTAL, reused = 0;

    BeginLayout(&Route120_Layout, FALSE);
    cells = gOverworldTilemapBuffer_Bg3;
    for (u32 i = 0; i < legacyCells; i++)
        cells[i] = TILE_CACHE_FIRST_SLOT + i;

    gMapHeader.mapLayout = &TileCacheTest_Layout;
    TileCache_SwitchLayout(&TileCacheTest_Layout);
    // The previous map's cells cover the animation slots, so they only get pinned once those cells are gone.
    EXPECT(!TileCache_AllowsAnimDest(TEST_ANIM_FIRST_SLOT, TEST_ANIM_SLOTS));

    overflows = TileCache_GetOverflowCount();
    for (u32 i = legacyCells; TileCache_GetOverflowCount() == overflows; i++)
    {
        u32 slot;
        WriteVirtualTile(&cells[i], tile++);
        slot = cells[i] & TILEMAP_TILE_NUM_MASK;
        reused += (slot >= TILE_CACHE_FIRST_SLOT && slot < TILE_CACHE_FIRST_SLOT + legacyCells);
    }
    EXPECT_EQ(reused, 0);

    for (u32 i = 0; i < legacyCells; i++)
        TileCache_WriteCell(&cells[i], 0, 0);
    gMain.vblankCounter1++;
    TileCache_WriteCell(&cells[0], 0, 0);
    EXPECT(TileCache_AllowsAnimDest(TEST_ANIM_FIRST_SLOT, TEST_ANIM_SLOTS));
    EndLayout();
}

static const struct MapEvents sNoEvents = {0};

// Pixels and palettes of every tilemap cell, so two views can be compared however their tiles are stored.
static u32 HashView(bool32 streamed)
{
    u16 *const tilemaps[] = {gOverworldTilemapBuffer_Bg1, gOverworldTilemapBuffer_Bg2, gOverworldTilemapBuffer_Bg3};
    u32 hash = 2166136261u;
    u8 pixels[64];

    for (u32 layer = 0; layer < ARRAY_COUNT(tilemaps); layer++)
    {
        for (u32 cell = 0; cell < BG_SCREEN_SIZE / sizeof(u16); cell++)
        {
            u16 word = tilemaps[layer][cell];
            if (streamed)
                RenderCell(word, pixels);
            else
                RenderLegacyWord(&TileCacheTest_Layout, word, pixels);
            for (u32 i = 0; i < ARRAY_COUNT(pixels); i++)
                hash = (hash ^ pixels[i]) * 16777619u;
            hash = (hash ^ (word >> 12)) * 16777619u;
        }
    }
    return hash;
}

static const s8 sCameraSteps[][2] =
{
    {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0},
    {0, 1}, {0, 1}, {0, 1}, {0, 1},
    {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0},
};

static void WalkCamera(const struct MapLayout *layout, bool32 streamed, u32 *hashes)
{
    BeginLayout(layout, streamed);
    gMapHeader.events = &sNoEvents;
    gMapHeader.mapScripts = NULL;
    gMapHeader.connections = NULL;
    gSaveBlock1Ptr->pos.x = 24;
    gSaveBlock1Ptr->pos.y = 20;
    InitMap();
    ResetFieldCamera();
    ResetCameraUpdateInfo();
    DrawWholeMapView();
    hashes[0] = HashView(streamed);
    for (u32 i = 0; i < ARRAY_COUNT(sCameraSteps); i++)
    {
        gFieldCamera.movementSpeedX = sCameraSteps[i][0] * 16;
        gFieldCamera.movementSpeedY = sCameraSteps[i][1] * 16;
        CameraUpdateNoObjectRefresh();
        gMain.vblankCounter1++;
        hashes[i + 1] = HashView(streamed);
    }
    EndLayout();
}

// The test layout is Route 120 with its right half drawn from twins of the metatiles, so the two maps look the same.
TEST("Tile cache: Scrolling a streamed map draws exactly what the legacy renderer draws for the same view")
{
    u32 legacy[ARRAY_COUNT(sCameraSteps) + 1];
    u32 streamed[ARRAY_COUNT(sCameraSteps) + 1];

    WalkCamera(&Route120_Layout, FALSE, legacy);
    WalkCamera(&TileCacheTest_Layout, TRUE, streamed);
    EXPECT_NE(legacy[0], legacy[6]);
    for (u32 i = 0; i < ARRAY_COUNT(legacy); i++)
        EXPECT_EQ(streamed[i], legacy[i]);
    EXPECT_EQ(TileCache_GetOverflowCount(), 0);
}

static const u16 *GetLogicalPalette(const struct MapLayout *layout, u32 logical)
{
    const struct TilesetPaletteExtension *extension = gTilesetPaletteExtensions;

    if (logical < NUM_PALS_IN_PRIMARY)
        return layout->primaryTileset->palettes[logical];
    if (logical < NUM_PALS_TOTAL)
        return layout->secondaryTileset->palettes[logical];
    while (extension->tileset != layout->secondaryTileset)
        extension++;
    return extension->extraPalettes[logical - NUM_PALS_TOTAL];
}

// The source tile a cell must show, straight from the tilesets' tile data, whichever VRAM slot it is in.
static void RenderVirtualTile(const struct MapLayout *layout, u16 word, u32 ext, u8 *pixels)
{
    u32 tile = (word & TILEMAP_TILE_NUM_MASK) | (((ext & METATILE_EXT_TILE_HIGH_MASK) >> METATILE_EXT_TILE_HIGH_SHIFT) << 10);
    bool32 secondary = (tile >= NUM_TILES_IN_PRIMARY && tile < NUM_TILES_TOTAL) || tile >= NUM_TILES_TOTAL + NUM_TILES_IN_PRIMARY;
    u32 local = tile < NUM_TILES_TOTAL ? tile % NUM_TILES_IN_PRIMARY : (secondary ? tile - NUM_TILES_TOTAL : tile - NUM_TILES_IN_PRIMARY);
    const u8 *src = (const u8 *)GetTilesetCapacityInfo(secondary ? layout->secondaryTileset : layout->primaryTileset)->rawTiles + TILE_OFFSET_4BPP(local);

    for (u32 i = 0; i < 64; i++)
        pixels[i] = TilePixel(src, word, i % 8, i / 8);
}

struct ViewCheck
{
    u32 wrongPixels;
    u32 wrongPalettes;
    u32 extraPaletteCells;
    u32 extendedTileCells;
};

// Compares the cells of the camera's metatile ring with the metatiles the map holds there. Scrolling only redraws the
// first 15 rows and columns of the 16x16 ring; the last ones keep whatever scrolled out until the next full redraw.
#define MAINTAINED_RING_TILES 30

static void CheckView(const struct MapLayout *layout, u32 ringTopRow, struct ViewCheck *check)
{
    static const u8 sLayerTiles[] = {8, 4, 0};
    u16 *const tilemaps[] = {gOverworldTilemapBuffer_Bg1, gOverworldTilemapBuffer_Bg2, gOverworldTilemapBuffer_Bg3};
    u8 expected[64], actual[64];

    for (u32 row = 0; row < MAINTAINED_RING_TILES; row++)
    {
        for (u32 column = 0; column < MAINTAINED_RING_TILES; column++)
        {
            u32 metatile = MapGridGetMetatileIdAt(gSaveBlock1Ptr->pos.x + column / 2, gSaveBlock1Ptr->pos.y + row / 2);
            const struct Tileset *tileset = metatile < NUM_METATILES_IN_PRIMARY ? layout->primaryTileset : layout->secondaryTileset;
            u32 local = metatile < NUM_METATILES_IN_PRIMARY ? metatile : metatile - NUM_METATILES_IN_PRIMARY;
            const u8 *ext = GetTilesetCapacityInfo(tileset)->tileExt;
            const u8 *references = MapPalettesGetReferences(tileset, local);

            for (u32 layer = 0; layer < ARRAY_COUNT(tilemaps); layer++)
            {
                u32 k = sLayerTiles[layer] + (column & 1) + (row & 1) * 2;
                u32 source = local * NUM_TILES_PER_METATILE + k;
                u16 word = tileset->metatiles[source];
                u16 cell = tilemaps[layer][((row + ringTopRow) % 32) * 32 + column];
                u32 logical = references != NULL ? references[k] : word >> 12;
                const u16 *colors = GetLogicalPalette(layout, logical);

                RenderVirtualTile(layout, word, ext != NULL ? ext[source] : 0, expected);
                RenderCell(cell, actual);
                check->wrongPixels += memcmp(expected, actual, sizeof(actual)) != 0;
                check->wrongPalettes += memcmp(&gPlttBufferUnfaded[BG_PLTT_ID(cell >> 12) + 1], &colors[1], 15 * sizeof(u16)) != 0;
                check->extraPaletteCells += logical >= NUM_PALS_TOTAL;
                check->extendedTileCells += ext != NULL && (ext[source] & METATILE_EXT_TILE_HIGH_MASK);
            }
        }
    }
}

#define STEPS_INTO_TINTED_CORNER 10

// The bottom-right corner of the test layout draws the mirrored tiles with the four extra palettes. Walking into it
// streams tiles and swaps palettes on the same cells.
TEST("Tile cache: Streamed tiles and extra map palettes draw together while walking into the tinted corner")
{
    struct ViewCheck before = {0}, after = {0};

    ResetSpriteData();
    FreeAllSpritePalettes();
    StartWeather();
    ResetPaletteFadeControl();
    gWeatherPtr->colorMapIndex = 0;

    BeginLayout(&TileCacheTest_Layout, TRUE);
    gMapHeader.mapType = MAP_TYPE_ROUTE;
    gMapHeader.events = &sNoEvents;
    gMapHeader.mapScripts = NULL;
    gMapHeader.connections = NULL;
    MapPalettesLoad(&TileCacheTest_Layout);
    EXPECT(MapPalettesActive());
    // Map rows 27-42 of the right half: mirrored tiles, normal palettes. Ten steps down reach the tinted rows from 44.
    gSaveBlock1Ptr->pos.x = 27;
    gSaveBlock1Ptr->pos.y = 34;
    InitMap();
    ResetFieldCamera();
    ResetCameraUpdateInfo();
    DrawWholeMapView();
    MapPalettesEndFrame();
    CheckView(&TileCacheTest_Layout, 0, &before);

    for (u32 i = 0; i < STEPS_INTO_TINTED_CORNER; i++)
    {
        MapPalettesCommit();
        gMain.vblankCounter1++;
        MapPalettesBeginFrame();
        gFieldCamera.movementSpeedX = 0;
        gFieldCamera.movementSpeedY = 16;
        CameraUpdateNoObjectRefresh();
        MapPalettesEndFrame();
    }
    CheckView(&TileCacheTest_Layout, (STEPS_INTO_TINTED_CORNER * 2) % 32, &after);

    EXPECT_EQ(before.wrongPixels, 0);
    EXPECT_EQ(before.wrongPalettes, 0);
    EXPECT_EQ(before.extraPaletteCells, 0);
    EXPECT_GT(before.extendedTileCells, 0);
    EXPECT_EQ(after.wrongPixels, 0);
    EXPECT_EQ(after.wrongPalettes, 0);
    EXPECT_GT(after.extraPaletteCells, 0);
    EXPECT_GT(after.extendedTileCells, 0);
    EXPECT_EQ(TileCache_GetOverflowCount(), 0);

    MapPalettesReset();
    EndLayout();
    ResetPaletteFadeControl();
    DestroyTask(gWeatherPtr->taskId);
    FreeAllSpritePalettes();
}

TEST("Tile cache: A released slot waits for the extended palette system to transfer the tilemaps")
{
    u16 *cells;
    u32 overflows, filled, releasedSlot, tile = NUM_TILES_TOTAL;

    ResetSpriteData();
    FreeAllSpritePalettes();
    StartWeather();
    ResetPaletteFadeControl();
    BeginLayout(&TileCacheTest_Layout, TRUE);
    gMapHeader.mapType = MAP_TYPE_ROUTE;
    MapPalettesLoad(&TileCacheTest_Layout);
    EXPECT(MapPalettesActive());
    MapPalettesEndFrame();
    MapPalettesCommit();

    cells = gOverworldTilemapBuffer_Bg3;
    overflows = TileCache_GetOverflowCount();
    for (filled = 0; TileCache_GetOverflowCount() == overflows; filled++)
        WriteVirtualTile(&cells[filled], tile++);

    releasedSlot = cells[0] & TILEMAP_TILE_NUM_MASK;
    MapPalettesSetMetatile(0, 0, cells, NULL);
    TileCache_WriteCell(&cells[0], 0, 0);
    MapPalettesEndFrame();
    gMain.vblankCounter1++;

    // Prepared but not yet transferred: VRAM still shows the released tile.
    WriteVirtualTile(&cells[filled], tile++);
    EXPECT_EQ(cells[filled] & TILEMAP_TILE_NUM_MASK, 0);

    MapPalettesCommit();
    gMain.vblankCounter1++;
    WriteVirtualTile(&cells[filled + 1], tile++);
    EXPECT_EQ(cells[filled + 1] & TILEMAP_TILE_NUM_MASK, releasedSlot);

    MapPalettesReset();
    EndLayout();
    ResetPaletteFadeControl();
    DestroyTask(gWeatherPtr->taskId);
    FreeAllSpritePalettes();
}

// Sheet-local boundary tiles of both tilesets and the game's virtual ids for them (see include/tile_cache.h).
static const u16 sBoundaryTiles[][3] =
{
    // {is secondary, sheet-local index, virtual id}
    {FALSE, 511, 511},
    {FALSE, 512, 1024},
    {FALSE, 1023, 1535},
    {TRUE, 511, 1023},
    {TRUE, 512, 1536},
    {TRUE, 1023, 2047},
    {TRUE, 1024, 2048},
    {TRUE, 2047, 3071},
};

TEST("Tile cache: Boundary tiles of a 1024-tile primary and a 2048-tile secondary come from the right source tile")
{
    const struct TilesetCapacityInfo *primary = GetTilesetCapacityInfo(TileCacheTest_Layout.primaryTileset);
    const struct TilesetCapacityInfo *secondary = GetTilesetCapacityInfo(TileCacheTest_Layout.secondaryTileset);
    u16 *cells;

    EXPECT_EQ(primary->numTiles, NUM_TILES_IN_PRIMARY_EXTENDED);
    EXPECT_EQ(secondary->numTiles, NUM_TILES_IN_SECONDARY_EXTENDED);
    BeginLayout(&TileCacheTest_Layout, TRUE);
    cells = gOverworldTilemapBuffer_Bg3;
    for (u32 i = 0; i < ARRAY_COUNT(sBoundaryTiles); i++)
    {
        const struct TilesetCapacityInfo *info = sBoundaryTiles[i][0] ? secondary : primary;
        const u8 *source = (const u8 *)info->rawTiles + TILE_OFFSET_4BPP(sBoundaryTiles[i][1]);
        u32 slot;

        WriteVirtualTile(&cells[i], sBoundaryTiles[i][2]);
        slot = cells[i] & TILEMAP_TILE_NUM_MASK;
        if (sBoundaryTiles[i][2] < NUM_TILES_IN_PRIMARY)
            EXPECT_EQ(slot, sBoundaryTiles[i][2]);
        else
            EXPECT(slot >= TILE_CACHE_FIRST_SLOT && slot < TILE_CACHE_END_SLOT);
        EXPECT_EQ(memcmp((const void *)(BG_VRAM + TILE_OFFSET_4BPP(slot)), source, TILE_SIZE_4BPP), 0);
    }
    EXPECT_EQ(TileCache_GetOverflowCount(), 0);
    EndLayout();
}

TEST("Tile cache: Streaming replaces only the tile number, keeping flips and the palette")
{
    u16 *cells;
    u16 word = 0xF000 | (1 << 10) | (1 << 11) | (2048 & TILEMAP_TILE_NUM_MASK);

    BeginLayout(&TileCacheTest_Layout, TRUE);
    cells = gOverworldTilemapBuffer_Bg3;
    TileCache_WriteCell(&cells[0], word, (2048 >> 10) << METATILE_EXT_TILE_HIGH_SHIFT);
    EXPECT_EQ(cells[0] & ~TILEMAP_TILE_NUM_MASK, word & ~TILEMAP_TILE_NUM_MASK);
    EXPECT_NE(cells[0] & TILEMAP_TILE_NUM_MASK, word & TILEMAP_TILE_NUM_MASK);
    EXPECT_EQ(TileCache_Resolve(word, (2048 >> 10) << METATILE_EXT_TILE_HIGH_SHIFT), cells[0]);
    EndLayout();
}

TEST("Tile cache: Virtual ids past the 3072 a tileset pair can hold draw the empty tile")
{
    u16 *cells;

    BeginLayout(&TileCacheTest_Layout, TRUE);
    cells = gOverworldTilemapBuffer_Bg3;
    for (u32 tile = NUM_VIRTUAL_TILES; tile < NUM_VIRTUAL_TILES + 8; tile++)
    {
        WriteVirtualTile(&cells[0], tile);
        EXPECT_EQ(cells[0] & TILEMAP_TILE_NUM_MASK, 0);
    }
    EXPECT_EQ(TileCache_GetOverflowCount(), 0);
    EndLayout();
}

// Crossing a connection redraws the view and reloads every map bank when the palette library changes, and keeps
// the allocation when it continues.
TEST("Tile cache: Connections reload palettes and redraw only when the palette library changes")
{
    ResetSpriteData();
    FreeAllSpritePalettes();
    StartWeather();
    ResetPaletteFadeControl();
    BeginLayout(&TileCacheTest_Layout, TRUE);
    gMapHeader.mapType = MAP_TYPE_ROUTE;
    TileCache_TakeRedrawRequest();

    MapPalettesLoad(&TileCacheTest_Layout);
    EXPECT(MapPalettesActive());
    EXPECT(MapPalettesCanContinue(&TileCacheTest_Layout));
    EXPECT_EQ(LoadConnectedMapTilesetPalettes(&TileCacheTest_Layout), NUM_PALS_IN_PRIMARY);
    EXPECT(!TileCache_TakeRedrawRequest());
    EXPECT(MapPalettesActive());

    // Into a legacy map: the extended allocation may have moved primary palettes, so they are reloaded too.
    EXPECT(!MapPalettesCanContinue(&Route120_Layout));
    EXPECT_EQ(LoadConnectedMapTilesetPalettes(&Route120_Layout), 0);
    EXPECT(TileCache_TakeRedrawRequest());
    EXPECT(!MapPalettesActive());
    EXPECT_EQ(memcmp(&gPlttBufferUnfaded[BG_PLTT_ID(1)], Route120_Layout.primaryTileset->palettes[1], PLTT_SIZE_4BPP), 0);

    // Legacy to legacy is the vanilla path.
    EXPECT_EQ(LoadConnectedMapTilesetPalettes(&Route120_Layout), NUM_PALS_IN_PRIMARY);
    EXPECT(!TileCache_TakeRedrawRequest());

    // Into an extended library: its allocation starts empty, so every cell is redrawn into it.
    EXPECT_EQ(LoadConnectedMapTilesetPalettes(&TileCacheTest_Layout), NUM_PALS_IN_PRIMARY);
    EXPECT(TileCache_TakeRedrawRequest());
    EXPECT(MapPalettesActive());

    MapPalettesReset();
    EndLayout();
    ResetPaletteFadeControl();
    DestroyTask(gWeatherPtr->taskId);
    FreeAllSpritePalettes();
}
