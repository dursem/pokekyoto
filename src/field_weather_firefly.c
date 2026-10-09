#include "global.h"
#include "palette.h"
#include "overworld.h"
#include "main.h"
#include "field_weather.h"
#include "gpu_regs.h"
#include "sprite.h"

// Firefly Shade darkens the overworld through a hardware color effect and uses
// moving circular OBJ windows to reveal the original palette around ten
// persistent, world-anchored firefly sprites.

#define NUM_FIREFLY_SPRITES 10
#define FIREFLY_WRAP_MARGIN 8
#define FIREFLY_FADE_FRAMES (3 * 60)
#define FIREFLY_LIGHT_OFFSET 4
#define FIREFLY_SHADE_LEVEL 3
#define FIREFLY_LIGHT_FRAME_TILES 4

static void CreateFireflySprites(void);
static void DestroyFireflySprites(void);
static void UpdateFireflySprite(struct Sprite *sprite);
static void SyncFireflyLightSprite(struct Sprite *sprite, u8 frame);
static void SetFireflyScreenPos(struct Sprite *sprite, s16 screenX, s16 screenY);
static s16 GetFireflyScreenX(struct Sprite *sprite);
static s16 GetFireflyScreenY(struct Sprite *sprite);
static u16 AdvanceFireflySeed(u16 seed);
static u8 ReduceFireflyValue(u8 value, u8 limit);
static void UpdateFireflyFadeIn(void);
static void SetFireflyBlendMode(bool8 enabled);
static void ConfigureFireflyLighting(void);
static void ApplyFireflyLighting(void);
static void RestoreFireflyLighting(void);

static EWRAM_DATA struct Sprite *sFireflySprites[NUM_FIREFLY_SPRITES];
static EWRAM_DATA struct Sprite *sFireflyLightSprites[NUM_FIREFLY_SPRITES];
static u16 sFireflyFadeTimer;
static u8 sFireflyBlendEVA;
static u8 sFireflyFadeStartEVA;
static u8 sFireflyShadeLevel;
static u8 sFireflyFadeStartShadeLevel;
static bool8 sFireflyFadingIn;
static bool8 sFireflyFadingOut;
static bool8 sFireflyLightingConfigured;
// Release OBJ resources for battles/menus, but remember the map's completed entrance.
static bool8 sFireflyResumeActive;
static u8 sFireflyResumeMapGroup;
static u8 sFireflyResumeMapNum;
static u16 sFireflySavedWindowBits;
static u16 sFireflySavedWinOut;
static u16 sFireflySavedBlendControl;
static u16 sFireflySavedBrightness;

static const u8 sWeatherFireflyTiles[] =
    INCBIN_U8("graphics/weather/firefly.4bpp");

static const u8 sWeatherFireflyLightTiles[] =
    INCBIN_U8("graphics/weather/firefly_light.4bpp");

static const u16 sWeatherFireflyPalette[] =
    INCBIN_U16("graphics/weather/firefly.gbapal");

static const struct SpriteSheet sFireflySpriteSheet =
{
    .data = sWeatherFireflyTiles,
    .size = sizeof(sWeatherFireflyTiles),
    .tag = GFXTAG_FIREFLY,
};

static const struct SpriteSheet sFireflyLightSpriteSheet =
{
    .data = sWeatherFireflyLightTiles,
    .size = sizeof(sWeatherFireflyLightTiles),
    .tag = GFXTAG_FIREFLY_LIGHT,
};

static const struct SpritePalette sFireflySpritePalette =
{
    .data = sWeatherFireflyPalette,
    .tag = PALTAG_FIREFLY,
};

static const struct OamData sFireflySpriteOamData =
{
    .y = 0,
    .affineMode = ST_OAM_AFFINE_OFF,
    .objMode = ST_OAM_OBJ_BLEND,
    .mosaic = FALSE,
    .bpp = ST_OAM_4BPP,
    .shape = SPRITE_SHAPE(8x8),
    .x = 0,
    .matrixNum = 0,
    .size = SPRITE_SIZE(8x8),
    .tileNum = 0,
    // Keep BG0 windows (map names and dialogue) in front of the fireflies.
    .priority = 1,
    .paletteNum = 0,
    .affineParam = 0,
};

// The nontransparent pixels of this invisible OBJ define a circular hardware
// window. Shade is applied outside the window and disabled inside it.
static const struct OamData sFireflyLightSpriteOamData =
{
    .y = 0,
    .affineMode = ST_OAM_AFFINE_OFF,
    .objMode = ST_OAM_OBJ_WINDOW,
    .mosaic = FALSE,
    .bpp = ST_OAM_4BPP,
    .shape = SPRITE_SHAPE(16x16),
    .x = 0,
    .matrixNum = 0,
    .size = SPRITE_SIZE(16x16),
    .tileNum = 0,
    // The invisible mask must win overlapping OBJ priority comparisons.
    .priority = 0,
    .paletteNum = 0,
    .affineParam = 0,
};

static const union AnimCmd sFireflySpriteAnimCmd[] =
{
    ANIMCMD_FRAME(0, 1),
    ANIMCMD_JUMP(0),
};

static const union AnimCmd *const sFireflySpriteAnimCmds[] =
{
    sFireflySpriteAnimCmd,
};

static const struct SpriteTemplate sFireflySpriteTemplate =
{
    .tileTag = GFXTAG_FIREFLY,
    .paletteTag = PALTAG_FIREFLY,
    .oam = &sFireflySpriteOamData,
    .anims = sFireflySpriteAnimCmds,
    .images = NULL,
    .affineAnims = gDummySpriteAffineAnimTable,
    .callback = UpdateFireflySprite,
};

static const struct SpriteTemplate sFireflyLightSpriteTemplate =
{
    .tileTag = GFXTAG_FIREFLY_LIGHT,
    .paletteTag = PALTAG_FIREFLY,
    .oam = &sFireflyLightSpriteOamData,
    .anims = sFireflySpriteAnimCmds,
    .images = NULL,
    .affineAnims = gDummySpriteAffineAnimTable,
    .callback = SpriteCallbackDummy,
};

static const struct Coords16 sFireflyStartCoords[NUM_FIREFLY_SPRITES] =
{
    { 18,  25},
    { 58,  53},
    {106,  23},
    {155,  47},
    {218,  28},
    { 32, 112},
    { 78, 139},
    {132, 101},
    {186, 132},
    {226,  96},
};

static const u8 sFireflyPhaseStart[NUM_FIREFLY_SPRITES] =
{
      0,  32,  75, 123, 160,
    205,  18,  92, 147, 230,
};

static const s8 sFireflyDriftX[32] =
{
    -4, -4, -3, -3, -2, -2, -1,  0,
     1,  2,  3,  4,  4,  5,  5,  5,
     5,  5,  4,  4,  3,  2,  1,  0,
    -1, -2, -3, -4, -4, -5, -5, -5,
};

static const s8 sFireflyDriftY[32] =
{
     0, -1, -1, -2, -2, -2, -1, -1,
     0,  1,  2,  2,  3,  3,  2,  2,
     1,  0, -1, -2, -2, -3, -3, -2,
    -2, -1,  0,  1,  1,  2,  1,  1,
};

static const u8 sFireflyFlickerFrame[8] =
{
    0, 1, 2, 2, 1, 0, 1, 2,
};

#define tFireflyPhase    data[0]
#define tFireflyBaseTile data[1]
#define tFireflySeed     data[2]
#define tFireflyMode     data[3]
#define tFireflyIndex    data[4]
#define tLightBaseTile   data[0]

void FireflyShade_InitVars(void)
{
    bool8 resume = sFireflyResumeActive
                && sFireflyResumeMapGroup == gSaveBlock1Ptr->location.mapGroup
                && sFireflyResumeMapNum == gSaveBlock1Ptr->location.mapNum;
    sFireflyResumeActive = FALSE;
    sFireflyResumeMapGroup = gSaveBlock1Ptr->location.mapGroup;
    sFireflyResumeMapNum = gSaveBlock1Ptr->location.mapNum;
    // Firefly Shade uses hardware brightness outside circular OBJ windows.
    // Keeping the weather color map at 0 leaves the true map palette available
    // inside every moving light circle.
    gWeatherPtr->initStep = 0;
    gWeatherPtr->targetColorMapIndex = 0;
    gWeatherPtr->colorMapStepDelay = 20;
    sFireflyFadeTimer = resume ? FIREFLY_FADE_FRAMES : 0;
    sFireflyBlendEVA = resume ? 16 : 0;
    sFireflyShadeLevel = resume ? FIREFLY_SHADE_LEVEL : 0;
    sFireflyFadingIn = !resume;
    sFireflyFadingOut = FALSE;
    Weather_SetBlendCoeffs(resume ? 8 : 0, resume ? BASE_SHADOW_INTENSITY : 16);
    gWeatherPtr->noShadows = !resume;
    gWeatherPtr->weatherGfxLoaded = resume;
}

void FireflyShade_InitAll(void)
{
    FireflyShade_InitVars();
    CreateFireflySprites();
    ConfigureFireflyLighting();
}

void FireflyShade_Main(void)
{
    // Recreate missing particles after an overworld sprite reset.
    CreateFireflySprites();
    ConfigureFireflyLighting();
    UpdateFireflyFadeIn();
    ApplyFireflyLighting();
}

bool8 FireflyShade_Finish(void)
{
    switch (gWeatherPtr->finishStep)
    {
    case 0:
        sFireflyResumeActive = FALSE;
        sFireflyFadingIn = FALSE;
        sFireflyFadingOut = TRUE;
        sFireflyFadeTimer = 0;
        sFireflyFadeStartEVA = sFireflyBlendEVA;
        sFireflyFadeStartShadeLevel = sFireflyShadeLevel;
        SetFireflyBlendMode(TRUE);
        gWeatherPtr->noShadows = TRUE;
        gWeatherPtr->finishStep++;
        return TRUE;
    case 1:
        if (sFireflyFadeTimer < FIREFLY_FADE_FRAMES)
            sFireflyFadeTimer++;

        sFireflyBlendEVA = sFireflyFadeStartEVA
                          - (sFireflyFadeTimer * sFireflyFadeStartEVA
                             / FIREFLY_FADE_FRAMES);
        sFireflyShadeLevel = sFireflyFadeStartShadeLevel
                           - (sFireflyFadeTimer * sFireflyFadeStartShadeLevel
                              / FIREFLY_FADE_FRAMES);
        Weather_SetBlendCoeffs(sFireflyBlendEVA, 16 - sFireflyBlendEVA);
        ApplyFireflyLighting();

        if (sFireflyFadeTimer < FIREFLY_FADE_FRAMES)
            return TRUE;

        DestroyFireflySprites();
        sFireflyFadingOut = FALSE;
        sFireflyShadeLevel = 0;
        // The particles are normal OBJs now; keep Kyoto lamps/shadows translucent.
        Weather_SetBlendCoeffs(8, BASE_SHADOW_INTENSITY);
        gWeatherPtr->noShadows = FALSE;
        gWeatherPtr->weatherGfxLoaded = TRUE;
        gWeatherPtr->finishStep++;
        break;
    }

    return FALSE;
}

static void CreateFireflySprites(void)
{
    u8 i;
    u8 paletteIndex;
    u8 spriteId;
    struct Sprite *sprite;
    struct Sprite *lightSprite;

    if (GetSpriteTileStartByTag(GFXTAG_FIREFLY) == 0xFFFF)
    {
        if (!CanAllocSpriteTiles(sizeof(sWeatherFireflyTiles) / TILE_SIZE_4BPP))
            return;
        LoadSpriteSheet(&sFireflySpriteSheet);
    }
    if (GetSpriteTileStartByTag(GFXTAG_FIREFLY_LIGHT) == 0xFFFF)
    {
        if (!CanAllocSpriteTiles(sizeof(sWeatherFireflyLightTiles) / TILE_SIZE_4BPP))
            return;
        LoadSpriteSheet(&sFireflyLightSpriteSheet);
    }

    paletteIndex = IndexOfSpritePaletteTag(PALTAG_FIREFLY);
    if (paletteIndex == 0xFF)
    {
        LoadSpritePalette(&sFireflySpritePalette);
        paletteIndex = IndexOfSpritePaletteTag(PALTAG_FIREFLY);

    }

    if (paletteIndex == 0xFF)
        return;

    for (i = 0; i < NUM_FIREFLY_SPRITES; i++)
    {
        if (sFireflySprites[i] == NULL
         || !sFireflySprites[i]->inUse
         || sFireflySprites[i]->callback != UpdateFireflySprite)
        {
            sFireflySprites[i] = NULL;
            spriteId = CreateSpriteAtEnd(&sFireflySpriteTemplate, 0, 0, 0xFF);
            if (spriteId != MAX_SPRITES)
            {
                sprite = &gSprites[spriteId];
                sFireflySprites[i] = sprite;
                sprite->coordOffsetEnabled = TRUE;
                if (!sFireflyFadingIn)
                    sprite->oam.objMode = ST_OAM_OBJ_NORMAL;

                // Each firefly selects its glow tile independently.
                sprite->animPaused = TRUE;
                sprite->tFireflyBaseTile = sprite->oam.tileNum;
                sprite->tFireflyPhase = sFireflyPhaseStart[i];
                sprite->tFireflySeed = i * 37 + 11;
                sprite->tFireflyMode = i & 3;
                sprite->tFireflyIndex = i;

                SetFireflyScreenPos(
                    sprite,
                    sFireflyStartCoords[i].x,
                    sFireflyStartCoords[i].y
                );
            }
        }

        if (GetSpriteTileStartByTag(GFXTAG_FIREFLY_LIGHT) != 0xFFFF
         && (sFireflyLightSprites[i] == NULL
         || !sFireflyLightSprites[i]->inUse
         || sFireflyLightSprites[i]->oam.objMode != ST_OAM_OBJ_WINDOW))
        {
            sFireflyLightSprites[i] = NULL;
            spriteId = CreateSpriteAtEnd(&sFireflyLightSpriteTemplate, 0, 0, 0xFF);
            if (spriteId != MAX_SPRITES)
            {
                lightSprite = &gSprites[spriteId];
                sFireflyLightSprites[i] = lightSprite;
                lightSprite->coordOffsetEnabled = TRUE;
                lightSprite->animPaused = TRUE;
                lightSprite->tLightBaseTile = lightSprite->oam.tileNum;
            }
        }

        if (sFireflySprites[i] != NULL && sFireflyLightSprites[i] != NULL)
        {
            sprite = sFireflySprites[i];
            SyncFireflyLightSprite(
                sprite,
                sFireflyFlickerFrame[(sprite->tFireflyPhase >> 5) & 7]
            );
        }
    }
}

static void UpdateFireflyFadeIn(void)
{
    if (!sFireflyFadingIn)
        return;

    if (sFireflyFadeTimer < FIREFLY_FADE_FRAMES)
        sFireflyFadeTimer++;

    sFireflyBlendEVA = sFireflyFadeTimer * 16 / FIREFLY_FADE_FRAMES;
    sFireflyShadeLevel = sFireflyFadeTimer * FIREFLY_SHADE_LEVEL
                       / FIREFLY_FADE_FRAMES;
    Weather_SetBlendCoeffs(sFireflyBlendEVA, 16 - sFireflyBlendEVA);

    if (sFireflyFadeTimer >= FIREFLY_FADE_FRAMES)
    {
        sFireflyFadingIn = FALSE;
        sFireflyBlendEVA = 16;
        SetFireflyBlendMode(FALSE);
        // The particles are normal OBJs now; keep Kyoto lamps/shadows translucent.
        Weather_SetBlendCoeffs(8, BASE_SHADOW_INTENSITY);
        gWeatherPtr->noShadows = FALSE;
        gWeatherPtr->weatherGfxLoaded = TRUE;
    }
}

static void SetFireflyBlendMode(bool8 enabled)
{
    u8 i;

    for (i = 0; i < NUM_FIREFLY_SPRITES; i++)
    {
        if (sFireflySprites[i] != NULL
         && sFireflySprites[i]->inUse
         && sFireflySprites[i]->callback == UpdateFireflySprite)
        {
            sFireflySprites[i]->oam.objMode = enabled
                                                ? ST_OAM_OBJ_BLEND
                                                : ST_OAM_OBJ_NORMAL;
        }
    }
}

static void DestroyFireflySprites(void)
{
    u8 i;
    u8 paletteIndex;

    for (i = 0; i < NUM_FIREFLY_SPRITES; i++)
    {
        if (sFireflySprites[i] != NULL
         && sFireflySprites[i]->inUse
         && sFireflySprites[i]->callback == UpdateFireflySprite)
        {
            DestroySprite(sFireflySprites[i]);
        }

        sFireflySprites[i] = NULL;

        if (sFireflyLightSprites[i] != NULL
         && sFireflyLightSprites[i]->inUse
         && sFireflyLightSprites[i]->oam.objMode == ST_OAM_OBJ_WINDOW)
        {
            DestroySprite(sFireflyLightSprites[i]);
        }

        sFireflyLightSprites[i] = NULL;
    }

    if (GetSpriteTileStartByTag(GFXTAG_FIREFLY) != 0xFFFF)
        FreeSpriteTilesByTag(GFXTAG_FIREFLY);
    if (GetSpriteTileStartByTag(GFXTAG_FIREFLY_LIGHT) != 0xFFFF)
        FreeSpriteTilesByTag(GFXTAG_FIREFLY_LIGHT);

    paletteIndex = IndexOfSpritePaletteTag(PALTAG_FIREFLY);
    if (paletteIndex != 0xFF)
    {
        FreeSpritePaletteByTag(PALTAG_FIREFLY);
    }

    RestoreFireflyLighting();
}

static void UpdateFireflySprite(struct Sprite *sprite)
{
    u8 driftIndex;
    u8 frame;
    s16 dx;
    s16 dy;
    s16 screenX;
    s16 screenY;

    sprite->tFireflyPhase = (sprite->tFireflyPhase + 1) & 0xFF;
    driftIndex = sprite->tFireflyPhase >> 3;

    dx = sFireflyDriftX[driftIndex];
    dy = sFireflyDriftY[driftIndex];

    if (sprite->tFireflyMode & 1)
        dx = -dx;
    if (sprite->tFireflyMode & 2)
        dy = -dy;

    sprite->x2 = dx;
    sprite->y2 = dy;

    frame = sFireflyFlickerFrame[(sprite->tFireflyPhase >> 5) & 7];
    sprite->oam.tileNum = sprite->tFireflyBaseTile + frame;
    SyncFireflyLightSprite(sprite, frame);

    // Do not wrap particles into the next map while they are fading out.
    if (sFireflyFadingOut)
        return;

    screenX = GetFireflyScreenX(sprite);
    screenY = GetFireflyScreenY(sprite);

    if (screenX < -FIREFLY_WRAP_MARGIN)
    {
        sprite->tFireflySeed = AdvanceFireflySeed(sprite->tFireflySeed);
        SetFireflyScreenPos(
            sprite,
            DISPLAY_WIDTH + FIREFLY_WRAP_MARGIN,
            8 + ReduceFireflyValue((u8)sprite->tFireflySeed, DISPLAY_HEIGHT - 16)
        );
    }
    else if (screenX > DISPLAY_WIDTH + FIREFLY_WRAP_MARGIN)
    {
        sprite->tFireflySeed = AdvanceFireflySeed(sprite->tFireflySeed);
        SetFireflyScreenPos(
            sprite,
            -FIREFLY_WRAP_MARGIN,
            8 + ReduceFireflyValue((u8)sprite->tFireflySeed, DISPLAY_HEIGHT - 16)
        );
    }

    screenY = GetFireflyScreenY(sprite);

    if (screenY < -FIREFLY_WRAP_MARGIN)
    {
        sprite->tFireflySeed = AdvanceFireflySeed(sprite->tFireflySeed);
        SetFireflyScreenPos(
            sprite,
            8 + ReduceFireflyValue((u8)sprite->tFireflySeed, DISPLAY_WIDTH - 16),
            DISPLAY_HEIGHT + FIREFLY_WRAP_MARGIN
        );
    }
    else if (screenY > DISPLAY_HEIGHT + FIREFLY_WRAP_MARGIN)
    {
        sprite->tFireflySeed = AdvanceFireflySeed(sprite->tFireflySeed);
        SetFireflyScreenPos(
            sprite,
            8 + ReduceFireflyValue((u8)sprite->tFireflySeed, DISPLAY_WIDTH - 16),
            -FIREFLY_WRAP_MARGIN
        );
    }

    SyncFireflyLightSprite(sprite, frame);
}

static void SyncFireflyLightSprite(struct Sprite *sprite, u8 frame)
{
    struct Sprite *lightSprite;
    u8 index = sprite->tFireflyIndex;

    if (index >= NUM_FIREFLY_SPRITES)
        return;

    lightSprite = sFireflyLightSprites[index];
    if (lightSprite == NULL
     || !lightSprite->inUse
     || lightSprite->oam.objMode != ST_OAM_OBJ_WINDOW)
        return;

    // Each circular mask extends exactly three pixels beyond its matching
    // 4x4, 6x6, or 8x8 firefly form. The center remains transparent beneath
    // the visible firefly so its alpha fade remains available.
    lightSprite->oam.tileNum = lightSprite->tLightBaseTile
                             + frame * FIREFLY_LIGHT_FRAME_TILES;
    SetFireflyScreenPos(
        lightSprite,
        GetFireflyScreenX(sprite) - FIREFLY_LIGHT_OFFSET,
        GetFireflyScreenY(sprite) - FIREFLY_LIGHT_OFFSET
    );
}

static void SetFireflyScreenPos(struct Sprite *sprite, s16 screenX, s16 screenY)
{
    sprite->x =
        screenX
        - gSpriteCoordOffsetX
        - sprite->centerToCornerVecX
        - sprite->x2;

    sprite->y =
        screenY
        - gSpriteCoordOffsetY
        - sprite->centerToCornerVecY
        - sprite->y2;
}

static s16 GetFireflyScreenX(struct Sprite *sprite)
{
    return sprite->x
         + sprite->x2
         + sprite->centerToCornerVecX
         + gSpriteCoordOffsetX;
}

static s16 GetFireflyScreenY(struct Sprite *sprite)
{
    return sprite->y
         + sprite->y2
         + sprite->centerToCornerVecY
         + gSpriteCoordOffsetY;
}

static u16 AdvanceFireflySeed(u16 seed)
{
    return seed * 109 + 89;
}

static u8 ReduceFireflyValue(u8 value, u8 limit)
{
    while (value >= limit)
        value -= limit;

    return value;
}

static void ConfigureFireflyLighting(void)
{
    if (!sFireflyLightingConfigured)
    {
        sFireflySavedWindowBits = GetGpuReg(REG_OFFSET_DISPCNT)
                               & (DISPCNT_WIN0_ON
                                | DISPCNT_WIN1_ON
                                | DISPCNT_OBJWIN_ON);
        sFireflySavedWinOut = GetGpuReg(REG_OFFSET_WINOUT);
        sFireflySavedBlendControl = GetGpuReg(REG_OFFSET_BLDCNT);
        sFireflySavedBrightness = GetGpuReg(REG_OFFSET_BLDY);
        sFireflyLightingConfigured = TRUE;
    }

    ApplyFireflyLighting();
}

static void ApplyFireflyLighting(void)
{
    if (!sFireflyLightingConfigured || gMain.callback2 != CB2_Overworld)
        return;
    // Iris transitions and hardware fades retain ownership of the windows.
    if ((gPaletteFade.active && gPaletteFade.mode == 2)
     || GetGpuReg(REG_OFFSET_WIN0H) != 0x00FF
     || GetGpuReg(REG_OFFSET_WIN0V) != 0x00FF
     || GetGpuReg(REG_OFFSET_WIN1V) != 0xFFFF)
        return;

    // The overworld normally leaves WIN0 enabled across the screen. WIN0 has
    // higher priority than OBJ windows, so it must be disabled while this
    // effect is active or it prevents both Shade and the moving cutouts.
    SetGpuReg(
        REG_OFFSET_DISPCNT,
        (GetGpuReg(REG_OFFSET_DISPCNT)
       & ~(DISPCNT_WIN0_ON | DISPCNT_WIN1_ON))
      | DISPCNT_OBJWIN_ON
    );

    // Outside the circular OBJ windows every layer is visible and color effects
    // are enabled. Inside the circles the same layers remain visible, but color
    // effects are disabled, revealing the original unshaded BG palette.
    SetGpuReg(
        REG_OFFSET_WINOUT,
        WINOUT_WIN01_ALL
      | WINOUT_WINOBJ_BG_ALL
      | WINOUT_WINOBJ_OBJ
    );

    SetGpuReg(
        REG_OFFSET_BLDCNT,
        BLDCNT_TGT1_BG1
      | BLDCNT_TGT1_BG2
      | BLDCNT_TGT1_BG3
      | BLDCNT_TGT1_OBJ
      | BLDCNT_EFFECT_DARKEN
      | BLDCNT_TGT2_ALL
    );
    SetGpuReg(REG_OFFSET_BLDY, sFireflyShadeLevel);
}

static void RestoreFireflyLighting(void)
{
    u16 dispcnt;

    if (!sFireflyLightingConfigured)
        return;

    dispcnt = GetGpuReg(REG_OFFSET_DISPCNT);
    dispcnt &= ~(DISPCNT_WIN0_ON | DISPCNT_WIN1_ON | DISPCNT_OBJWIN_ON);
    dispcnt |= sFireflySavedWindowBits;

    SetGpuReg(REG_OFFSET_DISPCNT, dispcnt);
    SetGpuReg(REG_OFFSET_WINOUT, sFireflySavedWinOut);
    SetGpuReg(REG_OFFSET_BLDCNT, sFireflySavedBlendControl);
    SetGpuReg(REG_OFFSET_BLDY, sFireflySavedBrightness);
    sFireflyLightingConfigured = FALSE;
}

#undef tFireflyPhase
#undef tFireflyBaseTile
#undef tFireflySeed
#undef tFireflyMode
#undef tFireflyIndex
#undef tLightBaseTile
#undef NUM_FIREFLY_SPRITES
#undef FIREFLY_WRAP_MARGIN
#undef FIREFLY_FADE_FRAMES
#undef FIREFLY_LIGHT_OFFSET
#undef FIREFLY_SHADE_LEVEL
#undef FIREFLY_LIGHT_FRAME_TILES

// Menu callbacks and sprite resets must release the effect before UI palettes
// and OBJ slots are reused. The weather initializer recreates it on return.
void FireflyShade_Reset(void)
{
    if (sFireflyLightingConfigured && !sFireflyFadingIn && !sFireflyFadingOut
     && sFireflyBlendEVA == 16 && gWeatherPtr->currWeather == WEATHER_SHADE)
    {
        sFireflyResumeActive = TRUE;
    }
    DestroyFireflySprites();
    sFireflyFadingIn = FALSE;
    sFireflyFadingOut = FALSE;
}

void FireflyShade_Prepare(void)
{
    ApplyFireflyLighting();
}
