#include "global.h"
#include "test/test.h"
#include "battle_util.h"
#include "field_weather.h"
#include "field_weather_petals.h"
#include "gpu_regs.h"
#include "kyoto_petals_config.h"
#include "main.h"
#include "malloc.h"
#include "map_palette.h"
#include "overworld.h"
#include "palette.h"
#include "sprite.h"
#include "task.h"
#include "tilesets.h"
#include "constants/battle.h"
#include "constants/map_groups.h"
#include "constants/rgb.h"
#include "constants/rtc.h"

// Use the actual General tree palette and engine allocator. Only the cells in
// the visible test cache are synthetic, so residency and bank moves are real.
struct PetalFixture
{
    MainCallback callback;
    struct MapHeader mapHeader;
    struct TimeBlendSettings timeBlend;
    struct WarpData location;
    struct Coords16 pos;
    struct TilesetPaletteExtension extension;
    struct MapPaletteProfile profile;
    u16 *tilemaps[3];
    u8 timeOfDay;
    u8 savedWeather;
};

static const u16 sCells[12] = {0};
static const u8 sTreeColors[] = {9, 10, 11};

static void DrawPaletteCell(u32 offset, u32 logical)
{
    u8 references[12];
    memset(references, logical, sizeof(references));
    MapPalettesBeginFrame();
    MapPalettesSetMetatile(offset, 0, sCells, NULL, references);
    MapPalettesEndFrame();
}

static void SetTestTime(u32 start, u32 end, u32 weight)
{
    gTimeBlend.startBlend = gTimeOfDayBlend[start];
    gTimeBlend.endBlend = gTimeOfDayBlend[end];
    gTimeBlend.weight = weight;
    gTimeBlend.altWeight = 256;
    gTimeOfDay = end;
}

static bool8 FinishPetalsForTest(u32 weather)
{
    return weather == WEATHER_AUTUMN_WIND ? AutumnWind_Finish() : Sakura_Finish();
}

static void DrainPetalsForTest(void)
{
    // The no-dither transition can take longer than the old fixed fade because
    // every particle keeps its normal speed until it reaches its wrap edge.
    for (u32 frame = 0; frame < 4096; frame++)
    {
        bool8 continuing = FinishPetalsForTest(gWeatherPtr->currWeather);
        AnimateSprites();
        Petals_BeforePaletteTransfer();
        if (!continuing)
            return;
    }
}

static const volatile u8 *GetPetalTilePixels(void)
{
    u32 tile = GetSpriteTileStartByTag(KYOTO_PETALS_PALETTE_TAG);
    if (tile == 0xFFFF)
        return NULL;
    return (const volatile u8 *)(OBJ_VRAM0 + tile * TILE_SIZE_4BPP);
}

static void BeginPetalFixture(struct PetalFixture *fixture, bool32 treeVisible)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->callback = gMain.callback2;
    fixture->mapHeader = gMapHeader;
    fixture->timeBlend = gTimeBlend;
    fixture->timeOfDay = gTimeOfDay;
    fixture->savedWeather = gSaveBlock1Ptr->weather;
    fixture->location = gSaveBlock1Ptr->location;
    fixture->pos = gSaveBlock1Ptr->pos;
    fixture->tilemaps[0] = gOverworldTilemapBuffer_Bg1;
    fixture->tilemaps[1] = gOverworldTilemapBuffer_Bg2;
    fixture->tilemaps[2] = gOverworldTilemapBuffer_Bg3;

    ResetSpriteData();
    FreeAllSpritePalettes();
    ResetPaletteFadeControl();
    StartWeather();
    gMapHeader = *Overworld_GetMapHeaderByGroupAndId(MAP_GROUP(MAP_LITTLEROOT_TOWN), MAP_NUM(MAP_LITTLEROOT_TOWN));
    gSaveBlock1Ptr->location.mapGroup = MAP_GROUP(MAP_LITTLEROOT_TOWN);
    gSaveBlock1Ptr->location.mapNum = MAP_NUM(MAP_LITTLEROOT_TOWN);
    gSaveBlock1Ptr->pos.x = 10;
    gSaveBlock1Ptr->pos.y = 10;
    gOverworldTilemapBuffer_Bg1 = AllocZeroed(BG_SCREEN_SIZE);
    gOverworldTilemapBuffer_Bg2 = AllocZeroed(BG_SCREEN_SIZE);
    gOverworldTilemapBuffer_Bg3 = AllocZeroed(BG_SCREEN_SIZE);
    fixture->extension.tileset = gMapHeader.mapLayout->secondaryTileset;
    fixture->profile.layout = gMapHeader.mapLayout;
    fixture->profile.pinnedPalettes = 1;
    MapPalettesLoadProfile(gMapHeader.mapLayout, &fixture->extension, &fixture->profile);
    SetTestTime(TIME_DAY, TIME_DAY, 256);
    gWeatherPtr->currWeather = WEATHER_SAKURA_PETALS;
    gWeatherPtr->nextWeather = WEATHER_SAKURA_PETALS;
    gSaveBlock1Ptr->weather = WEATHER_SAKURA_PETALS;
    gWeatherPtr->colorMapIndex = 0;
    gWeatherPtr->palProcessingState = WEATHER_PAL_STATE_IDLE;
    DrawPaletteCell(0, treeVisible ? 3 : 1);
    // Do not execute the overworld callback; only expose its rendering context.
    gMain.callback2 = CB2_Overworld;
    Sakura_InitAll();
}

static void EndPetalFixture(struct PetalFixture *fixture)
{
    DrainPetalsForTest();
    MapPalettesReset();
    FREE_AND_SET_NULL(gOverworldTilemapBuffer_Bg1);
    FREE_AND_SET_NULL(gOverworldTilemapBuffer_Bg2);
    FREE_AND_SET_NULL(gOverworldTilemapBuffer_Bg3);
    gOverworldTilemapBuffer_Bg1 = fixture->tilemaps[0];
    gOverworldTilemapBuffer_Bg2 = fixture->tilemaps[1];
    gOverworldTilemapBuffer_Bg3 = fixture->tilemaps[2];
    DestroyTask(gWeatherPtr->taskId);
    ResetPaletteFadeControl();
    FreeAllSpritePalettes();
    gWeatherPtr->currWeather = gWeatherPtr->nextWeather = WEATHER_NONE;
    gWeatherPtr->palProcessingState = WEATHER_PAL_STATE_IDLE;
    gMapHeader = fixture->mapHeader;
    gTimeBlend = fixture->timeBlend;
    gTimeOfDay = fixture->timeOfDay;
    gSaveBlock1Ptr->location = fixture->location;
    gSaveBlock1Ptr->pos = fixture->pos;
    gSaveBlock1Ptr->weather = fixture->savedWeather;
    gMain.callback2 = fixture->callback;
}

static bool32 PetalsMatchTree(u32 treeBank, u32 petalBank)
{
    if (treeBank >= 16 || petalBank >= 16)
        return FALSE;
    for (u32 i = 0; i < ARRAY_COUNT(sTreeColors); i++)
        if (gPlttBufferFaded[OBJ_PLTT_ID(petalBank) + i + 1]
         != gPlttBufferFaded[BG_PLTT_ID(treeBank) + sTreeColors[i]])
            return FALSE;
    return TRUE;
}

TEST("Petal weather: Sakura matches the tree through time, screen fades and bank relocation")
{
    struct PetalFixture fixture;
    bool32 exact = TRUE;
    u16 dayColor, nightColor;
    u32 bank, firstBank, petalBank;

    BeginPetalFixture(&fixture, TRUE);
    firstBank = bank = MapPalettesGetPhysicalBank(3);
    petalBank = IndexOfSpritePaletteTag(KYOTO_PETALS_PALETTE_TAG);
    // The allocator deliberately maps logical palette 3 somewhere other than 3.
    bool32 remapped = bank < 16 && bank != 3;
    dayColor = gPlttBufferFaded[BG_PLTT_ID(bank) + 10];
    Petals_BeforePaletteTransfer();
    exact &= PetalsMatchTree(bank, petalBank);

    // The map refresh can happen without an OBJ refresh. Compare what would
    // actually be transferred, including an interpolated dusk and full night.
    SetTestTime(TIME_DAY, TIME_NIGHT, 128);
    ApplyWeatherToNewMapPalette(bank);
    Petals_BeforePaletteTransfer();
    exact &= PetalsMatchTree(bank, petalBank);
    SetTestTime(TIME_NIGHT, TIME_NIGHT, 256);
    ApplyWeatherToNewMapPalette(bank);
    nightColor = gPlttBufferFaded[BG_PLTT_ID(bank) + 10];
    Petals_BeforePaletteTransfer();
    exact &= PetalsMatchTree(bank, petalBank);

    BeginTimeOfDayPaletteFade(PALETTES_ALL, 0, 8, 16,
                             &gTimeBlend.startBlend, &gTimeBlend.endBlend,
                             gTimeBlend.weight, RGB_BLACK);
    Petals_BeforePaletteTransfer();
    exact &= PetalsMatchTree(bank, petalBank);

    // Evict the tree, let another logical palette occupy its former bank, then
    // bring the tree back while the screen fade is still in progress.
    DrawPaletteCell(0, 1);
    bool32 absent = MapPalettesGetPhysicalBank(3) == MAP_PALETTE_NONE;
    DrawPaletteCell(2, 3);
    bank = MapPalettesGetPhysicalBank(3);
    bool32 moved = bank < 16 && bank != firstBank;
    Petals_BeforePaletteTransfer();
    exact &= PetalsMatchTree(bank, petalBank);
    EndPetalFixture(&fixture);

    EXPECT(remapped);
    EXPECT(absent);
    EXPECT(moved);
    EXPECT_NE(dayColor, nightColor);
    EXPECT(exact);
}

static void MenuCallbackForPetalTest(void) { }

TEST("Petal weather: absent tree and non-field callbacks cannot contaminate palettes")
{
    struct PetalFixture fixture;
    u16 before[PLTT_BUFFER_SIZE];
    bool32 absent, untouchedOffscreen, untouchedMenu;

    BeginPetalFixture(&fixture, FALSE);
    absent = MapPalettesGetPhysicalBank(3) == MAP_PALETTE_NONE;
    // A physical bank numbered 3 must not be mistaken for logical palette 3.
    gPlttBufferFaded[BG_PLTT_ID(3) + 9] = RGB(31, 0, 31);
    memcpy(before, gPlttBufferFaded, sizeof(before));
    Petals_BeforePaletteTransfer();
    untouchedOffscreen = !memcmp(before, gPlttBufferFaded, sizeof(before));

    DrawPaletteCell(2, 3);
    // Retain the allocated effect to exercise the callback guard independently
    // of the callback-transition cleanup hook.
    gMain.callback2 = MenuCallbackForPetalTest;
    memcpy(before, gPlttBufferFaded, sizeof(before));
    Petals_BeforePaletteTransfer();
    untouchedMenu = !memcmp(before, gPlttBufferFaded, sizeof(before));
    EndPetalFixture(&fixture);

    EXPECT(absent);
    EXPECT(untouchedOffscreen);
    EXPECT(untouchedMenu);
}

struct PetalAppearance
{
    s16 x, y, x2, y2;
    u16 tile;
    u8 spriteId;
};

static u32 CapturePetalAppearance(struct PetalAppearance *appearance)
{
    u32 count = 0;
    u32 palette = IndexOfSpritePaletteTag(KYOTO_PETALS_PALETTE_TAG);
    for (u32 i = 0; i < MAX_SPRITES; i++)
    {
        const struct Sprite *sprite = &gSprites[i];
        if (!sprite->inUse || palette >= 16 || sprite->oam.paletteNum != palette)
            continue;
        if (count < KYOTO_SAKURA_PETAL_COUNT)
        {
            appearance[count].x = sprite->x;
            appearance[count].y = sprite->y;
            appearance[count].x2 = sprite->x2;
            appearance[count].y2 = sprite->y2;
            appearance[count].tile = sprite->oam.tileNum - sprite->sheetTileStart;
            appearance[count].spriteId = i;
        }
        count++;
    }
    return count;
}

static bool32 AppearanceEqual(const struct PetalAppearance *a, const struct PetalAppearance *b)
{
    return a->x == b->x && a->y == b->y && a->x2 == b->x2 && a->y2 == b->y2
        && a->tile == b->tile;
}

static bool32 AppearanceSetContains(const struct PetalAppearance *haystack, u32 haystackCount,
                                    const struct PetalAppearance *needles, u32 needleCount)
{
    bool8 used[KYOTO_SAKURA_PETAL_COUNT] = {0};

    for (u32 i = 0; i < needleCount; i++)
    {
        bool32 found = FALSE;
        for (u32 j = 0; j < haystackCount; j++)
        {
            if (!used[j] && AppearanceEqual(&haystack[j], &needles[i]))
            {
                used[j] = TRUE;
                found = TRUE;
                break;
            }
        }
        if (!found)
            return FALSE;
    }
    return TRUE;
}

TEST("Petal weather: menu recreation and weather cleanup preserve unrelated sprites")
{
    struct PetalFixture fixture;
    struct PetalAppearance before[KYOTO_SAKURA_PETAL_COUNT] = {0};
    struct PetalAppearance after[KYOTO_SAKURA_PETAL_COUNT] = {0};
    const u16 otherColors[16] = {[0 ... 15] = RGB(5, 10, 20)};
    const u16 otherTag = 0x7011;
    u32 beforeCount, afterCount, autumnCount, liveAfterReset = 0;
    bool32 released, unrelatedSurvived, sameAppearance, finishReleased;
    bool32 resumedOutgoingWeather, keptDestinationWeather, continuedDrain;

    BeginPetalFixture(&fixture, TRUE);
    u32 otherPalette = AllocSpritePalette(otherTag);
    u32 otherSprite = CreateSprite(&gDummySpriteTemplate, 100, 80, 0);
    gSprites[otherSprite].oam.paletteNum = otherPalette;
    LoadPalette(otherColors, OBJ_PLTT_ID(otherPalette), sizeof(otherColors));

    gSaveBlock1Ptr->weather = WEATHER_SUNNY;
    SetNextWeather(WEATHER_SUNNY);
    for (u32 frame = 0; frame < 90; frame++)
    {
        Sakura_Finish();
        AnimateSprites();
        Petals_BeforePaletteTransfer();
    }
    beforeCount = CapturePetalAppearance(before);

    // Use the real callback switch: it snapshots the exact surviving population
    // and must not recreate particles that already drained away.
    SetMainCallback2(MenuCallbackForPetalTest);
    released = IndexOfSpritePaletteTag(KYOTO_PETALS_PALETTE_TAG) >= 16;
    for (u32 i = 0; i < MAX_SPRITES; i++)
        liveAfterReset += gSprites[i].inUse;
    unrelatedSurvived = gSprites[otherSprite].inUse
                    && gSprites[otherSprite].x == 100
                    && gSprites[otherSprite].y == 80
                    && IndexOfSpritePaletteTag(otherTag) == otherPalette
                    && !memcmp(otherColors, &gPlttBufferFaded[OBJ_PLTT_ID(otherPalette)], sizeof(otherColors));

    // Match the return-from-Bag path: saved weather names the destination, but
    // the outgoing Sakura population must be reconstructed first and keep draining.
    DestroyTask(gWeatherPtr->taskId);
    StartWeather();
    ResumePausedWeather();
    resumedOutgoingWeather = gWeatherPtr->currWeather == WEATHER_SAKURA_PETALS;
    keptDestinationWeather = gWeatherPtr->nextWeather == WEATHER_SUNNY;
    SetMainCallback2(CB2_Overworld);
    Sakura_InitAll();
    Petals_BeforePaletteTransfer();
    afterCount = CapturePetalAppearance(after);
    sameAppearance = afterCount == beforeCount
                  && AppearanceSetContains(after, afterCount, before, beforeCount);
    continuedDrain = Sakura_Finish();
    AnimateSprites();
    continuedDrain &= CapturePetalAppearance(after) <= afterCount;

    DrainPetalsForTest();
    gWeatherPtr->currWeather = gWeatherPtr->nextWeather = WEATHER_AUTUMN_WIND;
    AutumnWind_InitAll();
    autumnCount = CapturePetalAppearance(after);
    DrainPetalsForTest();
    finishReleased = IndexOfSpritePaletteTag(KYOTO_PETALS_PALETTE_TAG) >= 16
                  && gSprites[otherSprite].inUse
                  && IndexOfSpritePaletteTag(otherTag) == otherPalette
                  && !memcmp(otherColors, &gPlttBufferFaded[OBJ_PLTT_ID(otherPalette)], sizeof(otherColors));
    DestroySprite(&gSprites[otherSprite]);
    FreeSpritePaletteByTag(otherTag);
    EndPetalFixture(&fixture);

    EXPECT_GT(beforeCount, 0);
    EXPECT_LE(beforeCount, KYOTO_SAKURA_PETAL_COUNT);
    EXPECT_EQ(liveAfterReset, 1);
    EXPECT(released);
    EXPECT(unrelatedSurvived);
    EXPECT_EQ(afterCount, beforeCount);
    EXPECT(sameAppearance);
    EXPECT(resumedOutgoingWeather);
    EXPECT(keptDestinationWeather);
    EXPECT(continuedDrain);
    EXPECT_EQ(autumnCount, KYOTO_AUTUMN_LEAF_COUNT);
    EXPECT(finishReleased);
}

TEST("Petal weather: drain leaves source pixels and shared blending untouched")
{
    static const u8 weathers[] = {WEATHER_SAKURA_PETALS, WEATHER_AUTUMN_WIND};

    for (u32 effect = 0; effect < ARRAY_COUNT(weathers); effect++)
    {
        struct PetalFixture fixture;
        u32 weather = weathers[effect];
        u32 bytes = weather == WEATHER_SAKURA_PETALS ? 9 * 128 : 16 * 128;
        u32 expectedCount = weather == WEATHER_SAKURA_PETALS ? KYOTO_SAKURA_PETAL_COUNT : KYOTO_AUTUMN_LEAF_COUNT;
        struct PetalAppearance appearance[KYOTO_SAKURA_PETAL_COUNT] = {0};
        u8 *fullSheet = Alloc(bytes);
        u32 initialCount, previousCount, drops = 0, endFrame = 0;
        u16 blend;
        u8 eva, evb;
        bool32 monotonic = TRUE, pixelsStable = TRUE, blendStable = TRUE;

        EXPECT(fullSheet != NULL);
        BeginPetalFixture(&fixture, TRUE);
        if (weather == WEATHER_AUTUMN_WIND)
        {
            DrainPetalsForTest();
            gWeatherPtr->currWeather = gWeatherPtr->nextWeather = weather;
            AutumnWind_InitAll();
        }

        const volatile u8 *pixels = GetPetalTilePixels();
        EXPECT(pixels != NULL);
        for (u32 i = 0; i < bytes; i++)
            fullSheet[i] = pixels[i];
        initialCount = previousCount = CapturePetalAppearance(appearance);
        blend = GetGpuReg(REG_OFFSET_BLDALPHA);
        eva = gWeatherPtr->currBlendEVA;
        evb = gWeatherPtr->currBlendEVB;

        SetNextWeather(WEATHER_SUNNY);
        for (u32 frame = 1; frame <= 4096; frame++)
        {
            bool8 continuing = FinishPetalsForTest(weather);
            AnimateSprites();
            Petals_BeforePaletteTransfer();
            u32 count = CapturePetalAppearance(appearance);

            monotonic &= count <= previousCount;
            drops += count < previousCount;
            if (continuing)
            {
                const volatile u8 *livePixels = GetPetalTilePixels();
                pixelsStable &= livePixels != NULL;
                if (livePixels != NULL)
                    for (u32 i = 0; i < bytes; i++)
                        pixelsStable &= livePixels[i] == fullSheet[i];
            }
            else
            {
                endFrame = frame;
                break;
            }

            blendStable &= GetGpuReg(REG_OFFSET_BLDALPHA) == blend
                        && gWeatherPtr->currBlendEVA == eva && gWeatherPtr->currBlendEVB == evb
                        && gWeatherPtr->targetBlendEVA == eva && gWeatherPtr->targetBlendEVB == evb;
            previousCount = count;
        }

        bool32 released = IndexOfSpritePaletteTag(KYOTO_PETALS_PALETTE_TAG) >= 16
                       && GetSpriteTileStartByTag(KYOTO_PETALS_PALETTE_TAG) == 0xFFFF;
        FREE_AND_SET_NULL(fullSheet);
        EndPetalFixture(&fixture);

        EXPECT_EQ(initialCount, expectedCount);
        EXPECT(monotonic);
        EXPECT_GT(drops, 0);
        EXPECT_GT(endFrame, 0);
        EXPECT_LE(endFrame, 4096);
        EXPECT(released);
        EXPECT(pixelsStable);
        EXPECT(blendStable);
    }
}


TEST("Petal weather: surviving particles accelerate only after entering the destination map")
{
    struct PetalFixture fixture;
    s32 sourceBefore, sourceAfter, destinationAfter;
    s32 normalDelta, fastDelta;
    s32 expectedFastDelta;
    u8 id = MAX_SPRITES;

    BeginPetalFixture(&fixture, TRUE);
    SetNextWeather(WEATHER_SUNNY);

    // Start the outgoing drain while still on the source map. Pick a petal far
    // enough from the lower edge that neither of these two frames can retire it.
    Sakura_Finish();
    for (u32 i = 0; i < MAX_SPRITES; i++)
    {
        if (gSprites[i].inUse
         && gSprites[i].oam.paletteNum == IndexOfSpritePaletteTag(KYOTO_PETALS_PALETTE_TAG)
         && gSprites[i].y >= 32 && gSprites[i].y <= 120)
        {
            id = i;
            break;
        }
    }
    EXPECT_LT(id, MAX_SPRITES);
    if (id >= MAX_SPRITES)
    {
        EndPetalFixture(&fixture);
        return;
    }

    sourceBefore = gSprites[id].data[1];
    AnimateSprites();
    sourceAfter = gSprites[id].data[1];
    normalDelta = sourceAfter - sourceBefore;

    // The weather is still draining, but the saved location now names another
    // connected map. The very next sprite update must use the 1.6x exit speed.
    gSaveBlock1Ptr->location.mapNum++;
    Sakura_Finish();
    AnimateSprites();
    destinationAfter = gSprites[id].data[1];
    fastDelta = destinationAfter - sourceAfter;
    expectedFastDelta = normalDelta * KYOTO_PETALS_NEW_MAP_DRAIN_NUMERATOR
                      / KYOTO_PETALS_NEW_MAP_DRAIN_DENOMINATOR;

    EndPetalFixture(&fixture);

    EXPECT_GT(normalDelta, 0);
    EXPECT_EQ(fastDelta, expectedFastDelta);
    EXPECT_GT(fastDelta, normalDelta);
}

TEST("Petal weather: returning during drain keeps survivors and refills only after cancellation")
{
    struct PetalFixture fixture;
    struct PetalAppearance before[KYOTO_SAKURA_PETAL_COUNT] = {0};
    struct PetalAppearance after[KYOTO_SAKURA_PETAL_COUNT] = {0};
    u32 initialCount, beforeCount, afterCount;
    bool32 survivorsPreserved;

    BeginPetalFixture(&fixture, TRUE);
    initialCount = CapturePetalAppearance(before);
    SetNextWeather(WEATHER_SUNNY);

    // Let enough normal falling motion elapse for at least one existing petal
    // to leave. No replacement is allowed while the transition is outgoing.
    for (u32 frame = 0; frame < 800; frame++)
    {
        Sakura_Finish();
        AnimateSprites();
        if (CapturePetalAppearance(after) < initialCount)
            break;
    }
    beforeCount = CapturePetalAppearance(before);

    // Native route-back cancellation calls InitVars for the still-current
    // Sakura weather. Survivors remain; only then may missing density re-enter
    // from the normal offscreen edge.
    SetNextWeather(WEATHER_SAKURA_PETALS);
    Sakura_Main();
    afterCount = CapturePetalAppearance(after);
    survivorsPreserved = AppearanceSetContains(after, afterCount, before, beforeCount);
    EndPetalFixture(&fixture);

    EXPECT_EQ(initialCount, KYOTO_SAKURA_PETAL_COUNT);
    EXPECT_LT(beforeCount, initialCount);
    EXPECT_EQ(afterCount, initialCount);
    EXPECT(survivorsPreserved);
}

TEST("Petal weather: saved map IDs stay stable and leaves do not cause battle rain")
{
    static const enum OverworldWeather oldIds[] =
    {
        WEATHER_NONE, WEATHER_SUNNY_CLOUDS, WEATHER_SUNNY, WEATHER_RAIN,
        WEATHER_SNOW, WEATHER_RAIN_THUNDERSTORM, WEATHER_FOG_HORIZONTAL,
        WEATHER_VOLCANIC_ASH, WEATHER_SANDSTORM, WEATHER_FOG_DIAGONAL,
        WEATHER_UNDERWATER, WEATHER_SHADE, WEATHER_DROUGHT, WEATHER_DOWNPOUR,
        WEATHER_UNDERWATER_BUBBLES, WEATHER_ABNORMAL, WEATHER_ROUTE119_CYCLE,
        WEATHER_ROUTE123_CYCLE, WEATHER_FOG, WEATHER_DYNAMIC,
    };
    for (u32 i = 0; i < ARRAY_COUNT(oldIds); i++)
        EXPECT_EQ(oldIds[i], i);
    EXPECT_EQ(WEATHER_SAKURA_PETALS, 13);
    EXPECT_EQ(WEATHER_AUTUMN_WIND, 20);
    EXPECT_GE(WEATHER_COUNT, 21);
    EXPECT_EQ(GetWeatherFromOverworldWeather(WEATHER_SAKURA_PETALS), B_WEATHER_NONE);
    EXPECT_EQ(GetWeatherFromOverworldWeather(WEATHER_AUTUMN_WIND), B_WEATHER_NONE);
    EXPECT_EQ(GetWeatherFromOverworldWeather(WEATHER_RAIN), B_WEATHER_RAIN_NORMAL);
    EXPECT_EQ(GetWeatherFromOverworldWeather(WEATHER_RAIN_THUNDERSTORM), B_WEATHER_RAIN_NORMAL);
}
