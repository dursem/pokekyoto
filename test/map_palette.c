#include "global.h"
#include "test/test.h"
#include "map_palette.h"
#include "palette.h"
#include "overworld.h"
#include "field_weather.h"
#include "sprite.h"
#include "malloc.h"
#include "task.h"
#include "constants/map_types.h"
#include "constants/rgb.h"
#include "constants/rgb.h"

TEST("Map palettes allocate 14 banks while protecting pinned and reserved slots")
{
    u8 old[MAP_PALETTES_COUNT];
    u8 result[MAP_PALETTES_COUNT];
    memset(old, MAP_PALETTE_NONE, sizeof(old));
    EXPECT(MapPaletteAllocate(0x3FFF, 0, 1, old, result));
    for (u32 i = 0; i < 14; i++)
        EXPECT_EQ(result[i], i);
    EXPECT(!MapPaletteAllocate(0x7FFF, 0, 1, old, result));
    EXPECT(!MapPaletteAllocate(0x3FFF, 1 << 13, 1, old, result));
    EXPECT(!MapPaletteAllocate(1, 1, 1, old, result));
    EXPECT(!MapPaletteAllocate(1 << 18, 0, 0, old, result));
    EXPECT(!MapPaletteAllocate(0, 1 << 14, 0, old, result));
}

TEST("Map palettes keep surviving assignments when all four extra palettes replace old ones")
{
    u8 old[MAP_PALETTES_COUNT];
    u8 result[MAP_PALETTES_COUNT];
    for (u32 i = 0; i < MAP_PALETTES_COUNT; i++)
        old[i] = i < 14 ? i : MAP_PALETTE_NONE;
    EXPECT(MapPaletteAllocate(0x3FFFF & ~0x3C00, 0, 1, old, result));
    for (u32 i = 0; i < 10; i++)
        EXPECT_EQ(result[i], i);
    for (u32 i = 14; i < 18; i++)
        EXPECT_EQ(result[i], i - 4);
    EXPECT(MapPaletteAllocate((1 << 17) | (1 << 3), (1 << 13), 1 | (1 << 8), result, old));
    EXPECT_EQ(old[17], 1); // Its previous bank 13 is now reserved.
    EXPECT_EQ(old[3], 3);
    EXPECT_EQ(old[8], 8);
}

static const u16 sBase[14][16] = {[0 ... 13] = {[0 ... 15] = RGB(0, 31, 0)}};
static const u16 sExtra[4][16] = {
    {[0 ... 15] = RGB(31, 0, 0)},
    {[0 ... 15] = RGB(0, 0, 31)},
    {[0 ... 15] = RGB(31, 31, 0)},
    {[0 ... 15] = RGB(31, 0, 31)},
};
static const u16 *const sNight[4] = {NULL};
static const u8 sRefs[12] = {14, 15, 16, 17, 14, 15, 16, 17, 14, 15, 16, 17};
static const u16 sTiles[12] = {[0 ... 11] = 0x0D23};
static const struct Tileset sPrimary = {.palettes = sBase};
static const struct Tileset sSecondary = {.isSecondary = TRUE, .palettes = sBase};
static const struct MapLayout sLayout = {.primaryTileset = &sPrimary, .secondaryTileset = &sSecondary};
static const struct TilesetPaletteExtension sExtension = {
    .tileset = &sSecondary, .extraPalettes = sExtra, .extraNightPalettes = sNight,
    .references = sRefs, .metatileCount = 1, .extraWeatherTypes = {1, 1, 1, 1},
};
static const struct MapPaletteProfile sProfile = {.layout = &sLayout, .pinnedPalettes = 1};

static void DrawTestCache(const u8 *references)
{
    for (u32 y = 0; y < 32; y += 2)
        for (u32 x = 0; x < 32; x += 2)
            MapPalettesSetMetatile(y * 32 + x, 0, sTiles, references);
}

TEST("Map palettes render four extras and reconstruct an evicted palette during a fade")
{
    u8 replacements[12] = {[0 ... 11] = 1};
    ResetSpriteData();
    FreeAllSpritePalettes();
    StartWeather();
    ResetPaletteFadeControl();
    gMapHeader.mapType = MAP_TYPE_INDOOR;
    gMapHeader.mapLayout = &sLayout;
    gWeatherPtr->colorMapIndex = 0;
    gOverworldTilemapBuffer_Bg1 = AllocZeroed(BG_SCREEN_SIZE);
    gOverworldTilemapBuffer_Bg2 = AllocZeroed(BG_SCREEN_SIZE);
    gOverworldTilemapBuffer_Bg3 = AllocZeroed(BG_SCREEN_SIZE);
    MapPalettesLoadProfile(&sLayout, &sExtension, &sProfile);
    for (u32 i = 0; i < MAP_PALETTE_CELLS; i++)
        gOverworldTilemapBuffer_Bg1[i] = 0x0D23;
    DrawTestCache(sRefs);
    MapPalettesEndFrame();
    static const u8 offsets[] = {0, 1, 32, 33};
    for (u32 i = 0; i < 4; i++)
    {
        u16 tile = gOverworldTilemapBuffer_Bg1[offsets[i]];
        EXPECT_EQ(tile & 0xFFF, 0xD23);
        EXPECT_EQ(gPlttBufferFaded[BG_PLTT_ID(tile >> 12) + 1], sExtra[i][1]);
    }
    // Evict all extra palettes, begin a half-black fade, then bring them back.
    DrawTestCache(replacements);
    MapPalettesEndFrame();
    BeginNormalPaletteFade(PALETTES_ALL, 0, 8, 16, RGB_BLACK);
    DrawTestCache(sRefs);
    MapPalettesEndFrame();
    EXPECT_EQ(gPlttBufferFaded[BG_PLTT_ID(gOverworldTilemapBuffer_Bg1[0] >> 12) + 1], RGB(15, 0, 0));
    EXPECT_EQ(MapPalettesWeatherType(gOverworldTilemapBuffer_Bg1[0] >> 12, COLOR_MAP_NONE), COLOR_MAP_DARK_CONTRAST);
    struct Benchmark unchanged;
    struct Benchmark evicted;
    DrawTestCache(sRefs);
    BENCHMARK(&unchanged) { MapPalettesEndFrame(); }
    DrawTestCache(replacements);
    BENCHMARK(&evicted) { MapPalettesEndFrame(); }
    Test_MgbaPrintf("Map palette prepare: stable %d cycles, eviction %d cycles", unchanged.ticks * 64, evicted.ticks * 64);
    EXPECT_LT(unchanged.ticks * 64, 12000);
    EXPECT_LT(evicted.ticks * 64, 160000);
    ResetPaletteFadeControl();
    gWeatherPtr->currWeather = WEATHER_RAIN;
    gWeatherPtr->colorMapIndex = 3;
    DrawTestCache(sRefs);
    MapPalettesEndFrame();
    u16 weatherColor = gPlttBufferFaded[BG_PLTT_ID(gOverworldTilemapBuffer_Bg1[0] >> 12) + 1];
    DrawTestCache(replacements);
    MapPalettesEndFrame();
    BeginFastPaletteFade(FAST_FADE_OUT_TO_BLACK);
    DrawTestCache(sRefs);
    MapPalettesEndFrame();
    u16 fadedColor = gPlttBufferFaded[BG_PLTT_ID(gOverworldTilemapBuffer_Bg1[0] >> 12) + 1];
    for (u32 shift = 0; shift < 15; shift += 5)
        EXPECT_EQ((fadedColor >> shift) & 31, max(0, ((weatherColor >> shift) & 31) - 2));
    MapPalettesBeginFrame();
    EXPECT(MapPalettesBlockTransfer());
    MapPalettesEndFrame();
    MapPalettesReset();
    FREE_AND_SET_NULL(gOverworldTilemapBuffer_Bg1);
    FREE_AND_SET_NULL(gOverworldTilemapBuffer_Bg2);
    FREE_AND_SET_NULL(gOverworldTilemapBuffer_Bg3);
    ResetPaletteFadeControl();
    DestroyTask(gWeatherPtr->taskId);
    FreeAllSpritePalettes();
}
