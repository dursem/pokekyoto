#include "global.h"
#include "battle.h"
#include "fake_rtc.h"
#include "field_camera.h"
#include "field_weather.h"
#include "kyoto_blood_moon.h"
#include "main.h"
#include "overworld.h"
#include "palette.h"
#include "rtc.h"
#include "trig.h"
#include "constants/rgb.h"

enum
{
    BLOOD_MOON_FADE_OUT_LEAD_SECONDS = 4,
    SPRITE_FADE_START = 4,
    EMBER_MARGIN = 12,
    EMBER_TRAVEL = DISPLAY_HEIGHT + 2 * EMBER_MARGIN,
    CLOUD_BODY_WIDTH = 64,
    CLOUD_EDGE_WIDTH = 32,
    CLOUD_CELL_HEIGHT = 64,
    CLOUD_WIDTH = CLOUD_BODY_WIDTH + CLOUD_EDGE_WIDTH,
    CLOUD_REPEAT_WIDTH = DISPLAY_WIDTH + CLOUD_WIDTH + 16,
    CLOUD_REPEAT_HEIGHT = DISPLAY_HEIGHT + CLOUD_CELL_HEIGHT + 16,
    CLOUD_FRAMES_PER_PIXEL = 3,
    MAX_CLOUD_SHADE = 3,
    MAX_STRENGTH = 16,
    FRAMES_PER_FADE_STEP = 8,
};

static const u8 sEmberTiles[] = INCGFX_U8("graphics/weather/blood_moon/ember.png", ".4bpp");
static const u16 sEmberPalette[] = INCGFX_U16("graphics/weather/blood_moon/ember.png", ".gbapal");
static const u8 sCloudShadowTiles[] = INCGFX_U8("graphics/weather/blood_moon/shadows.png", ".4bpp");
static const u8 sCloudShadowEdgeTiles[] = INCGFX_U8("graphics/weather/blood_moon/shadow_edges.png", ".4bpp");

#define EMBER_FRAME(n) {sEmberTiles + (n) * TILE_SIZE_4BPP, TILE_SIZE_4BPP}
static const struct SpriteFrameImage sEmberFrames[] =
{
    EMBER_FRAME(0), EMBER_FRAME(1), EMBER_FRAME(2), EMBER_FRAME(3),
};
#undef EMBER_FRAME

static const struct SpriteFrameImage sCloudShadowFrames[] =
{
    overworld_frame(sCloudShadowTiles, 8, 8, 0),
    overworld_frame(sCloudShadowTiles, 8, 8, 1),
    overworld_frame(sCloudShadowTiles, 8, 8, 2),
    overworld_frame(sCloudShadowTiles, 8, 8, 3),
};

static const struct SpriteFrameImage sCloudShadowEdgeFrames[] =
{
    overworld_frame(sCloudShadowEdgeTiles, 4, 8, 0),
    overworld_frame(sCloudShadowEdgeTiles, 4, 8, 1),
    overworld_frame(sCloudShadowEdgeTiles, 4, 8, 2),
};

#define STILL_FRAME(n) static const union AnimCmd sFrame##n[] = {ANIMCMD_FRAME(n, 1), ANIMCMD_END}
STILL_FRAME(0);
STILL_FRAME(1);
STILL_FRAME(2);
STILL_FRAME(3);
#undef STILL_FRAME

static const union AnimCmd *const sAnims[] =
{
    sFrame0, sFrame1, sFrame2, sFrame3,
};

// The weather sits above the map and under the field's BG0 windows.
static const struct OamData sEmberOam = { .shape = ST_OAM_SQUARE, .size = ST_OAM_SIZE_0, .priority = 1 };

static const struct SpriteTemplate sEmberTemplate =
{
    .tileTag = TAG_NONE,
    .paletteTag = PALTAG_BLOOD_MOON,
    .oam = &sEmberOam,
    .anims = sAnims,
    .images = sEmberFrames,
};

// The cloud silhouettes are OBJ-window masks for the existing field shadow blend.
static const struct OamData sCloudShadowOam =
{
    .objMode = ST_OAM_OBJ_WINDOW,
    .shape = ST_OAM_SQUARE,
    .size = ST_OAM_SIZE_3,
    .priority = 1,
};

static const struct OamData sCloudShadowEdgeOam =
{
    .objMode = ST_OAM_OBJ_WINDOW,
    .shape = ST_OAM_V_RECTANGLE,
    .size = ST_OAM_SIZE_3,
    .priority = 1,
};

static const struct SpriteTemplate sCloudShadowTemplates[] =
{
    {
        .tileTag = TAG_NONE,
        .paletteTag = TAG_NONE,
        .oam = &sCloudShadowOam,
        .anims = sAnims,
        .images = sCloudShadowFrames,
    },
    {
        .tileTag = TAG_NONE,
        .paletteTag = TAG_NONE,
        .oam = &sCloudShadowEdgeOam,
        .anims = sAnims,
        .images = sCloudShadowEdgeFrames,
    },
};

static const struct
{
    u16 x, y;
    u8 cells;
} sCloudShadowDrift[] =
{
    { 62, 159, 2},
    {230,  37, 2},
    {195, 119, 2},
    { 25,   3, 1},
};

static const struct
{
    u8 x, y, speed, fadeStart;
} sEmberDrift[] =
{
    { 14, 140, 2,  4}, { 31,  77, 1,  8}, { 49, 112, 3, 10},
    { 67,  37, 2,  6}, { 87, 173, 2, 12}, {106,  84, 3,  5},
    {119,  29, 1,  8}, {143, 145, 2,  6}, {160,  56, 3, 11},
    {179, 123, 2,  7}, {204,  84, 1, 13}, {222, 166, 3,  9},
    { 24, 179, 3,  8}, { 63, 141, 1, 11}, {126, 125, 2, 10}, {195, 103, 3, 12},
};

static EWRAM_DATA struct
{
    u32 tick;
    u8 strength;
    u8 fadeFrame;
    u8 emberCount;
    u8 shadowCount;
    bool8 shadowAllocationTried;
    u8 emberIds[ARRAY_COUNT(sEmberDrift)];
    u8 shadowIds[ARRAY_COUNT(sCloudShadowFrames) + ARRAY_COUNT(sCloudShadowEdgeFrames)];
    u32 shadowStartFrame;
    s32 shadowMapOffsetX;
    s32 shadowMapOffsetY;
    bool8 scheduled;
    bool8 ending;
    bool8 debugOverride;
} sBloodMoon = {0};

// Keep the notice state across outdoor maps, interiors and field reloads.
static EWRAM_DATA struct
{
    s16 scheduledDay;
    bool8 scheduledNoticeShown;
    bool8 debugNoticeShown;
} sBloodMoonNotice = {0};

void BloodMoon_RefreshSchedule(void)
{
    sBloodMoon.scheduled = FALSE;
    sBloodMoon.ending = FALSE;
    if (RtcGetErrorStatus() & (RTC_INIT_ERROR | RTC_ERR_FLAG_MASK))
        return;

    RtcCalcLocalTime();
    if (!IsBetweenHours(gLocalTime.hours, BLOOD_MOON_START_HOUR, HOURS_PER_DAY)
     || !IsBetweenHours(gLocalTime.hours, NIGHT_HOUR_BEGIN, NIGHT_HOUR_END)
     || RtcGetLocalCalendarWeekday() != WEEKDAY_SUN)
        return;

    u32 secondsLeft = ((HOURS_PER_DAY - gLocalTime.hours) * MINUTES_PER_HOUR - gLocalTime.minutes)
                    * SECONDS_PER_MINUTE - gLocalTime.seconds;
    u32 secondsRatio = OW_USE_FAKE_RTC ? FakeRtc_GetSecondsRatio() : 1;
    sBloodMoon.scheduled = TRUE;
    sBloodMoon.ending = secondsLeft <= BLOOD_MOON_FADE_OUT_LEAD_SECONDS * secondsRatio;
}

u8 BloodMoon_GetEffectiveWeather(u8 weather)
{
    if ((sBloodMoon.scheduled || sBloodMoon.debugOverride) && MapHasNaturalLight(gMapHeader.mapType))
        return WEATHER_BLOOD_MOON;

    return weather;
}

void BloodMoon_SetDebugOverride(bool8 enabled)
{
    if (enabled && !sBloodMoon.debugOverride)
        sBloodMoonNotice.debugNoticeShown = FALSE;
    sBloodMoon.debugOverride = enabled;
}

bool32 BloodMoon_ShouldAnnounceRising(void)
{
    if (GetCurrentWeather() != WEATHER_BLOOD_MOON || !MapHasNaturalLight(gMapHeader.mapType))
        return FALSE;
    if (sBloodMoon.scheduled && !sBloodMoon.ending)
        return !sBloodMoonNotice.scheduledNoticeShown || sBloodMoonNotice.scheduledDay != gLocalTime.days;
    return sBloodMoon.debugOverride && !sBloodMoonNotice.debugNoticeShown;
}

void BloodMoon_MarkRisingAnnounced(void)
{
    if (sBloodMoon.scheduled && !sBloodMoon.ending)
    {
        sBloodMoonNotice.scheduledDay = gLocalTime.days;
        sBloodMoonNotice.scheduledNoticeShown = TRUE;
    }
    if (sBloodMoon.debugOverride)
        sBloodMoonNotice.debugNoticeShown = TRUE;
}

void BloodMoon_ResetSprites(void)
{
    // A full field reload already freed the sprites and their palette.
    sBloodMoon.emberCount = 0;
    sBloodMoon.shadowCount = 0;
    sBloodMoon.shadowAllocationTried = FALSE;
    if (BloodMoon_GetEffectiveWeather(WEATHER_NONE) != WEATHER_BLOOD_MOON)
    {
        sBloodMoon.strength = 0;
        sBloodMoon.fadeFrame = 0;
        sBloodMoon.tick = 0;
    }
}

static void DestroyCloudShadows(void)
{
    for (u32 i = 0; i < sBloodMoon.shadowCount; i++)
        DestroySprite(&gSprites[sBloodMoon.shadowIds[i]]);
    sBloodMoon.shadowCount = 0;
}

static void CreateCloudShadows(void)
{
    sBloodMoon.shadowAllocationTried = TRUE;
    for (u32 cloud = 0; cloud < ARRAY_COUNT(sCloudShadowDrift); cloud++)
    {
        u32 first = sBloodMoon.shadowCount;
        for (u32 part = 0; part < sCloudShadowDrift[cloud].cells; part++)
        {
            u32 id = CreateSpriteAtEndUnchecked(&sCloudShadowTemplates[part], 0, 0, 0);
            if (id == MAX_SPRITES)
            {
                // Keep complete silhouettes when the field is low on OBJ space.
                while (sBloodMoon.shadowCount > first)
                    DestroySprite(&gSprites[sBloodMoon.shadowIds[--sBloodMoon.shadowCount]]);
                return;
            }

            sBloodMoon.shadowIds[sBloodMoon.shadowCount++] = id;
            gSprites[id].invisible = TRUE;
            StartSpriteAnim(&gSprites[id], cloud);
        }
    }
}

static s32 WrapCloudCoordinate(s32 position, s32 period, s32 margin)
{
    s32 wrapped = (position + margin) % period;
    if (wrapped < 0)
        wrapped += period;
    return wrapped - margin;
}

void BloodMoon_OffsetCloudMapOrigin(s32 xTiles, s32 yTiles)
{
    if (sBloodMoon.strength < SPRITE_FADE_START)
        return;

    // Connected maps change the local coordinates while the terrain stays on screen.
    sBloodMoon.shadowMapOffsetX = (sBloodMoon.shadowMapOffsetX + xTiles * 16) % CLOUD_REPEAT_WIDTH;
    sBloodMoon.shadowMapOffsetY = (sBloodMoon.shadowMapOffsetY + yTiles * 16) % CLOUD_REPEAT_HEIGHT;
}

void BloodMoon_PositionCloudShadows(void)
{
    if (!sBloodMoon.shadowCount || GetCurrentWeather() != WEATHER_BLOOD_MOON)
        return;

    u32 frames = gMain.vblankCounter1 - sBloodMoon.shadowStartFrame;
    s32 drift = frames / CLOUD_FRAMES_PER_PIXEL;
    s32 viewX, viewY;
    GetFieldViewOriginPixels(&viewX, &viewY);
    viewX += sBloodMoon.shadowMapOffsetX;
    viewY += sBloodMoon.shadowMapOffsetY;

    u32 cell = 0;
    for (u32 cloud = 0; cell < sBloodMoon.shadowCount; cloud++)
    {
        // Equal steps along both map axes give a fixed diagonal across the terrain.
        // The repeat periods place the next copy outside the visible area.
        s32 left = WrapCloudCoordinate(sCloudShadowDrift[cloud].x + drift - viewX,
                                      CLOUD_REPEAT_WIDTH, CLOUD_WIDTH);
        s32 top = WrapCloudCoordinate(sCloudShadowDrift[cloud].y + drift - viewY,
                                     CLOUD_REPEAT_HEIGHT, CLOUD_CELL_HEIGHT);

        for (u32 part = 0; part < sCloudShadowDrift[cloud].cells; part++, cell++)
        {
            struct Sprite *sprite = &gSprites[sBloodMoon.shadowIds[cell]];
            s32 width = part ? CLOUD_EDGE_WIDTH : CLOUD_BODY_WIDTH;
            s32 x = left + (s32)part * CLOUD_BODY_WIDTH;
            sprite->x = x + width / 2;
            sprite->y = top + CLOUD_CELL_HEIGHT / 2;
            sprite->invisible = x <= -width || x >= DISPLAY_WIDTH
                             || top <= -CLOUD_CELL_HEIGHT || top >= DISPLAY_HEIGHT;
        }
    }
}

u32 BloodMoon_GetCloudShadowStrength(void)
{
    if (!sBloodMoon.shadowCount || GetCurrentWeather() != WEATHER_BLOOD_MOON)
        return 0;
    return min(sBloodMoon.strength / 4, MAX_CLOUD_SHADE);
}

static void CreateBloodMoonSprites(void)
{
    // Leave generated object events alone if the map already uses every palette.
    if (IndexOfSpritePaletteTag(TAG_NONE) == 0xFF)
        return;

    u32 palette = LoadSpritePaletteWithTag(sEmberPalette, PALTAG_BLOOD_MOON);
    if (palette == 0xFF)
        return;

    for (u32 i = 0; i < ARRAY_COUNT(sBloodMoon.emberIds); i++)
    {
        u32 id = CreateSpriteAtEndUnchecked(&sEmberTemplate, 0, 0, 0);
        if (id == MAX_SPRITES)
            break;
        sBloodMoon.emberIds[sBloodMoon.emberCount++] = id;
    }

    if (sBloodMoon.emberCount)
        UpdateSpritePaletteWithWeather(palette, FALSE);
    else
        FreeSpritePaletteByTag(PALTAG_BLOOD_MOON);
}

static void DestroyBloodMoonSprites(void)
{
    if (!sBloodMoon.emberCount)
        return;

    for (u32 i = 0; i < sBloodMoon.emberCount; i++)
        DestroySprite(&gSprites[sBloodMoon.emberIds[i]]);
    sBloodMoon.emberCount = 0;
    FreeSpritePaletteByTag(PALTAG_BLOOD_MOON);
}

static void UpdateBloodMoonSprites(void)
{
    u32 strength = sBloodMoon.strength;
    u32 tick = sBloodMoon.tick;
    if (strength < SPRITE_FADE_START)
    {
        DestroyBloodMoonSprites();
        DestroyCloudShadows();
        sBloodMoon.shadowAllocationTried = FALSE;
        return;
    }

    if (!sBloodMoon.shadowAllocationTried)
        CreateCloudShadows();
    if (!sBloodMoon.emberCount)
        CreateBloodMoonSprites();

    for (u32 i = 0; i < sBloodMoon.emberCount; i++)
    {
        struct Sprite *sprite = &gSprites[sBloodMoon.emberIds[i]];
        u32 phase = tick + i * 31;
        u32 rise = (tick * sEmberDrift[i].speed) >> 3;
        u32 y = (sEmberDrift[i].y + EMBER_TRAVEL - rise % EMBER_TRAVEL) % EMBER_TRAVEL;
        sprite->x = sEmberDrift[i].x + Sin(phase & 255, 8);
        sprite->y = (s32)y - EMBER_MARGIN;
        sprite->invisible = strength < sEmberDrift[i].fadeStart
                         || sprite->y < -2 || sprite->y >= DISPLAY_HEIGHT + 2;
        if (!sprite->invisible)
            StartSpriteAnimIfDifferent(sprite, (phase >> 4) % ARRAY_COUNT(sEmberFrames));
    }
}

static void AdvanceBloodMoon(bool32 active)
{
    u32 target = active ? MAX_STRENGTH : 0;
    if (!gPaletteFade.active && gWeatherPtr->palProcessingState == WEATHER_PAL_STATE_IDLE)
    {
        if (sBloodMoon.strength != target && ++sBloodMoon.fadeFrame == FRAMES_PER_FADE_STEP)
        {
            sBloodMoon.fadeFrame = 0;
            if (active)
            {
                sBloodMoon.strength++;
                if (sBloodMoon.strength == SPRITE_FADE_START)
                {
                    sBloodMoon.shadowStartFrame = gMain.vblankCounter1;
                    sBloodMoon.shadowMapOffsetX = 0;
                    sBloodMoon.shadowMapOffsetY = 0;
                }
            }
            else
                sBloodMoon.strength--;
            ApplyWeatherColorMapIfIdle(0);
        }
        if (sBloodMoon.strength)
            sBloodMoon.tick++;
        else
            sBloodMoon.tick = 0;
    }
    UpdateBloodMoonSprites();
}

void BloodMoon_InitAll(void)
{
    Sunny_InitAll();
    UpdateBloodMoonSprites();
}

void BloodMoon_Main(void)
{
    AdvanceBloodMoon(MapHasNaturalLight(gMapHeader.mapType)
                  && (sBloodMoon.debugOverride || (!sBloodMoon.ending && gTimeOfDay == TIME_NIGHT)));
}

bool8 BloodMoon_Finish(void)
{
    AdvanceBloodMoon(FALSE);
    return sBloodMoon.strength != 0;
}

// Called after time blending and before emissive colors and screen fades.
void BloodMoon_TintPalettes(u32 palettes)
{
    s32 strength = sBloodMoon.strength;
    if (!strength || GetCurrentWeather() != WEATHER_BLOOD_MOON || !MapHasNaturalLight(gMapHeader.mapType))
        return;

    for (u32 bank = 0; palettes; bank++, palettes >>= 1)
    {
        if (!(palettes & 1))
            continue;
        u16 *colors = &gPlttBufferFaded[PLTT_ID(bank)];
        for (u32 i = 0; i < 16; i++)
        {
            s32 red = GET_R(colors[i]);
            s32 green = GET_G(colors[i]);
            s32 blue = GET_B(colors[i]);
            s32 warmRed = min(31, red + ((red + green + blue + 4) >> 3));
            s32 mutedGreen = (green * 5 + 4) >> 3;
            s32 violetBlue = (blue * 4 + red + 4) >> 3;
            red += ((warmRed - red) * strength + 8) >> 4;
            green += ((mutedGreen - green) * strength + 8) >> 4;
            blue += ((violetBlue - blue) * strength + 8) >> 4;
            colors[i] = RGB2(red, green, blue);
        }
    }
}

enum
{
    BATTLE_BLOOD_MOON_GLOW = 8,
    BATTLE_BLOOD_MOON_CLOCK_INTERVAL = 60,
    BATTLE_BLOOD_MOON_EMBER_COUNT = 20,
    BATTLE_BLOOD_MOON_EMBER_MARGIN = 8,
    BATTLE_BLOOD_MOON_EMBER_HEIGHT = 118,
};

static const u16 sBattleEmberPalette[16] =
{
    RGB_BLACK,
    RGB(23, 6, 18),
    RGB(27, 10, 22),
    RGB(31, 14, 25),
    RGB(31, 21, 27),
    RGB(31, 28, 29),
};

static EWRAM_DATA struct
{
    u32 lastClockCheck;
    u8 glow;
    u8 emberCount;
    u8 emberIds[BATTLE_BLOOD_MOON_EMBER_COUNT];
} sBattleBloodMoon = {0};

static bool32 IsBloodMoonOverworldWeather(void)
{
    return GetCurrentWeather() == WEATHER_BLOOD_MOON
        && BloodMoon_GetEffectiveWeather(WEATHER_NONE) == WEATHER_BLOOD_MOON;
}

void BloodMoon_BattleStart(void)
{
    sBattleBloodMoon.glow = 0;
    sBattleBloodMoon.emberCount = 0;
    gBattleWeather = B_WEATHER_NONE;

    if (gBattleTypeFlags & (BATTLE_TYPE_LINK | BATTLE_TYPE_RECORDED | BATTLE_TYPE_FRONTIER | BATTLE_TYPE_SAFARI))
        return;
    if (GetCurrentWeather() != WEATHER_BLOOD_MOON)
        return;

    BloodMoon_RefreshSchedule();
    if (IsBloodMoonOverworldWeather())
    {
        gBattleWeather = B_WEATHER_BLOOD_MOON;
        sBattleBloodMoon.glow = BATTLE_BLOOD_MOON_GLOW;
        sBattleBloodMoon.lastClockCheck = gMain.vblankCounter1;
    }
}

static void SpriteCB_BattleBloodMoonEmber(struct Sprite *sprite)
{
    u32 index = sprite->data[0];
    u32 tick = gMain.vblankCounter1;
    u32 travel = BATTLE_BLOOD_MOON_EMBER_HEIGHT + 2 * BATTLE_BLOOD_MOON_EMBER_MARGIN;
    u32 rise = tick * (2 + index % 3) / 8;
    u32 y = (index * 43 + travel - rise % travel) % travel;
    u32 phase = tick + index * 31;

    sprite->x2 = Sin(phase & 255, 7);
    sprite->y = (s32)y - BATTLE_BLOOD_MOON_EMBER_MARGIN;
    sprite->invisible = sprite->y < 0 || sprite->y >= BATTLE_BLOOD_MOON_EMBER_HEIGHT
                     || sBattleBloodMoon.glow <= index % BATTLE_BLOOD_MOON_GLOW;
    if (!sprite->invisible)
        StartSpriteAnimIfDifferent(sprite, (phase >> 3) % ARRAY_COUNT(sEmberFrames));
}

static void CreateBattleBloodMoonEmbers(void)
{
    u32 palette = LoadSpritePaletteWithTag(sBattleEmberPalette, PALTAG_BLOOD_MOON);
    if (palette == 0xFF)
        return;
    for (u32 i = 0; i < ARRAY_COUNT(sBattleBloodMoon.emberIds); i++)
    {
        u32 x = BATTLE_BLOOD_MOON_EMBER_MARGIN
              + i * 59 % (DISPLAY_WIDTH - 2 * BATTLE_BLOOD_MOON_EMBER_MARGIN);
        u32 id = CreateSpriteAtEndUnchecked(&sEmberTemplate, x, 0, 100);
        if (id == MAX_SPRITES)
            break;
        struct Sprite *sprite = &gSprites[id];
        sprite->oam.priority = 2;
        sprite->invisible = TRUE;
        sprite->data[0] = i;
        sprite->callback = SpriteCB_BattleBloodMoonEmber;
        sBattleBloodMoon.emberIds[sBattleBloodMoon.emberCount++] = id;
    }
    if (sBattleBloodMoon.emberCount == 0)
        FreeSpritePaletteByTag(PALTAG_BLOOD_MOON);
}

void BloodMoon_BattleUpdate(void)
{
    u32 frame = gMain.vblankCounter1;

    if (gBattleWeather & B_WEATHER_BLOOD_MOON)
    {
        if (frame - sBattleBloodMoon.lastClockCheck >= BATTLE_BLOOD_MOON_CLOCK_INTERVAL)
        {
            sBattleBloodMoon.lastClockCheck = frame;
            BloodMoon_RefreshSchedule();
        }
        if (!IsBloodMoonOverworldWeather())
            gBattleWeather &= ~B_WEATHER_BLOOD_MOON;
    }

    if (sBattleBloodMoon.glow == 0)
        return;

    if (!(gBattleWeather & B_WEATHER_BLOOD_MOON) && frame % 4 == 0)
        sBattleBloodMoon.glow--;

    // Bag and Party rebuild OAM. Slots from before that reset may be in use again.
    if (sBattleBloodMoon.emberCount != 0)
    {
        const struct Sprite *first = &gSprites[sBattleBloodMoon.emberIds[0]];
        if (!first->inUse || first->callback != SpriteCB_BattleBloodMoonEmber)
            sBattleBloodMoon.emberCount = 0;
    }

    if (sBattleBloodMoon.glow == 0)
    {
        for (u32 i = 0; i < sBattleBloodMoon.emberCount; i++)
        {
            struct Sprite *sprite = &gSprites[sBattleBloodMoon.emberIds[i]];
            if (sprite->callback == SpriteCB_BattleBloodMoonEmber)
                DestroySprite(sprite);
        }
        sBattleBloodMoon.emberCount = 0;
        FreeSpritePaletteByTag(PALTAG_BLOOD_MOON);
    }
    else if (sBattleBloodMoon.emberCount == 0 && (gBattleWeather & B_WEATHER_BLOOD_MOON))
    {
        CreateBattleBloodMoonEmbers();
    }
}

u8 BloodMoon_GetBattleGlow(void)
{
    return sBattleBloodMoon.glow;
}
