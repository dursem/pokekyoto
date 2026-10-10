#include "global.h"
#include "decompress.h"
#include "field_camera.h"
#include "field_weather.h"
#include "fieldmap.h"
#include "main.h"
#include "malloc.h"
#include "map_palette.h"
#include "overworld.h"
#include "palette.h"
#include "sprite.h"
#include "task.h"
#include "tile_cache.h"
#include "test/test.h"
#include "constants/map_groups.h"

/*
 * Draws real maps through the field loader and renderer and prints a hash of what the screen shows: the top
 * background on its own (it covers object events) and the whole screen, top over middle over bottom over the
 * backdrop. Colours are read through palette RAM, so the hashes don't depend on which VRAM slot a tile or which
 * bank a palette was given. The same file builds against an older tree, so two runs' "Map render" lines can be
 * compared directly.
 */

static void LoadTilesetTiles(const struct Tileset *tileset, u32 firstSlot, u32 maxTiles)
{
    u32 size;
    void *buffer;

    if (tileset == NULL)
        return;
    if (!tileset->isCompressed)
    {
        CpuCopy16(tileset->tiles, (void *)(BG_VRAM + TILE_OFFSET_4BPP(firstSlot)), maxTiles * TILE_SIZE_4BPP);
        return;
    }
    size = GetDecompressedDataSize(tileset->tiles);
    buffer = AllocZeroed(size);
    DecompressDataWithHeaderWram(tileset->tiles, buffer);
    CpuCopy16(buffer, (void *)(BG_VRAM + TILE_OFFSET_4BPP(firstSlot)), min(size, maxTiles * TILE_SIZE_4BPP));
    Free(buffer);
}

static u32 CellPixel(u16 cell, u32 x, u32 y)
{
    const u8 *tile = (const u8 *)(BG_VRAM + TILE_OFFSET_4BPP(cell & 0x3FF));
    if (cell & (1 << 10))
        x = 7 - x;
    if (cell & (1 << 11))
        y = 7 - y;
    return (x & 1) ? tile[y * 4 + x / 2] >> 4 : tile[y * 4 + x / 2] & 0xF;
}

static u32 Mix(u32 hash, u32 value)
{
    return (hash ^ value) * 16777619u;
}

static void HashView(const char *name, u32 step)
{
    u32 top = 2166136261u, below = 2166136261u;

    for (u32 cell = 0; cell < BG_SCREEN_SIZE / sizeof(u16); cell++)
    {
        u16 bg1 = gOverworldTilemapBuffer_Bg1[cell], bg2 = gOverworldTilemapBuffer_Bg2[cell], bg3 = gOverworldTilemapBuffer_Bg3[cell];
        for (u32 y = 0; y < 8; y++)
        {
            for (u32 x = 0; x < 8; x++)
            {
                u32 index = CellPixel(bg1, x, y);
                top = Mix(top, index ? gPlttBufferUnfaded[BG_PLTT_ID(bg1 >> 12) + index] : 0x8000);
                if (index != 0)
                    below = Mix(below, gPlttBufferUnfaded[BG_PLTT_ID(bg1 >> 12) + index]);
                else if ((index = CellPixel(bg2, x, y)) != 0)
                    below = Mix(below, gPlttBufferUnfaded[BG_PLTT_ID(bg2 >> 12) + index]);
                else if ((index = CellPixel(bg3, x, y)) != 0)
                    below = Mix(below, gPlttBufferUnfaded[BG_PLTT_ID(bg3 >> 12) + index]);
                else
                    below = Mix(below, gPlttBufferUnfaded[0]);
            }
        }
    }
    Test_MgbaPrintf("Map render %s step %d: top %d screen %d", name, step, (s32)top, (s32)below);
}

static const s8 sSteps[][2] = {{1, 0}, {1, 0}, {1, 0}, {0, 1}, {0, 1}, {-1, 0}, {-1, 0}, {-1, 0}, {0, -1}};

static void RenderMap(const char *name, u32 group, u32 num)
{
    const struct MapLayout *layout;

    ResetSpriteData();
    FreeAllSpritePalettes();
    StartWeather();
    ResetPaletteFadeControl();
    gWeatherPtr->colorMapIndex = 0;
    REG_IME = 0;
    gMain.vblankCounter1 = 1;
    gMapHeader = *Overworld_GetMapHeaderByGroupAndId(group, num);
    layout = gMapHeader.mapLayout;
    gOverworldTilemapBuffer_Bg1 = AllocZeroed(BG_SCREEN_SIZE);
    gOverworldTilemapBuffer_Bg2 = AllocZeroed(BG_SCREEN_SIZE);
    gOverworldTilemapBuffer_Bg3 = AllocZeroed(BG_SCREEN_SIZE);
    CpuFill32(0, (void *)BG_VRAM, NUM_TILES_TOTAL * TILE_SIZE_4BPP);
    LoadTilesetTiles(layout->primaryTileset, 0, GetNumTilesInPrimary(layout));
    if (TileCache_LayoutIsStreamed(layout))
    {
        TileCache_LoadLayout(layout);
    }
    else
    {
        TileCache_Disable();
        LoadTilesetTiles(layout->secondaryTileset, GetNumTilesInPrimary(layout), NUM_TILES_TOTAL - GetNumTilesInPrimary(layout));
    }
    LoadMapTilesetPalettes(layout);
    gSaveBlock1Ptr->pos.x = layout->width / 2;
    gSaveBlock1Ptr->pos.y = layout->height / 2;
    InitMap();
    ResetFieldCamera();
    ResetCameraUpdateInfo();
    MapPalettesBeginFrame();
    DrawWholeMapView();
    MapPalettesEndFrame();
    HashView(name, 0);
    for (u32 i = 0; i < ARRAY_COUNT(sSteps); i++)
    {
        MapPalettesCommit();
        gMain.vblankCounter1++;
        MapPalettesBeginFrame();
        gFieldCamera.movementSpeedX = sSteps[i][0] * 16;
        gFieldCamera.movementSpeedY = sSteps[i][1] * 16;
        CameraUpdateNoObjectRefresh();
        MapPalettesEndFrame();
        HashView(name, i + 1);
    }
    EXPECT_EQ(TileCache_GetOverflowCount(), 0);
    MapPalettesReset();
    TileCache_Disable();
    FREE_AND_SET_NULL(gOverworldTilemapBuffer_Bg1);
    FREE_AND_SET_NULL(gOverworldTilemapBuffer_Bg2);
    FREE_AND_SET_NULL(gOverworldTilemapBuffer_Bg3);
    REG_IME = 1;
    ResetPaletteFadeControl();
    DestroyTask(gWeatherPtr->taskId);
    FreeAllSpritePalettes();
}

#define RENDER_TEST(map) TEST("Map render: " #map) { RenderMap(#map, MAP_GROUP(map), MAP_NUM(map)); }

RENDER_TEST(MAP_PETALBURG_CITY)
RENDER_TEST(MAP_LITTLEROOT_TOWN)
RENDER_TEST(MAP_ROUTE102)
RENDER_TEST(MAP_ROUTE103)
RENDER_TEST(MAP_RUSTBORO_CITY)
RENDER_TEST(MAP_ROUTE104)
RENDER_TEST(MAP_ROUTE116)
RENDER_TEST(MAP_PETALBURG_WOODS)
RENDER_TEST(MAP_ROUTE110)
RENDER_TEST(MAP_ROUTE119)
RENDER_TEST(MAP_ROUTE134)
RENDER_TEST(MAP_MAUVILLE_CITY)
RENDER_TEST(MAP_SLATEPORT_CITY)
RENDER_TEST(MAP_LILYCOVE_CITY)
RENDER_TEST(MAP_FORTREE_CITY)
RENDER_TEST(MAP_SOOTOPOLIS_CITY)
RENDER_TEST(MAP_PACIFIDLOG_TOWN)
RENDER_TEST(MAP_EVER_GRANDE_CITY)
RENDER_TEST(MAP_BATTLE_FRONTIER_OUTSIDE_WEST)
RENDER_TEST(MAP_PETALBURG_CITY_MART)
RENDER_TEST(MAP_LITTLEROOT_TOWN_BRENDANS_HOUSE_1F)
RENDER_TEST(MAP_GRANITE_CAVE_1F)
RENDER_TEST(MAP_SECRET_BASE_RED_CAVE1)
RENDER_TEST(MAP_SOOTOPOLIS_CITY_GYM_1F)
RENDER_TEST(MAP_EVER_GRANDE_CITY_SIDNEYS_ROOM)
RENDER_TEST(MAP_MAUVILLE_CITY_BIKE_SHOP)
