#include "global.h"
#include "field_weather.h"
#include "field_weather_petals.h"
#include "kyoto_petals_config.h"
#include "main.h"
#include "map_palette.h"
#include "overworld.h"
#include "palette.h"
#include "sprite.h"
#include "tilesets.h"
#include "trig.h"

// Both effects use fixed 16x16 cells, with four contiguous 8x8 tiles per pose.
// Sakura retains the approved small three-color artwork and nine tilt poses.
// Autumn Wind uses the two Peach leaf shapes, with eight poses per shape.
#define PETALS_TILE_TAG 0x1210
#define PETALS_FRAME_TILES 4
#define PETALS_MAX_COUNT KYOTO_SAKURA_PETAL_COUNT
#define SAKURA_ENTRY_Y (-16)
#define SAKURA_ENTRY_SPACING 8
#define SAKURA_HALF_HEIGHT 8
#define SAKURA_ENTERING (1 << 14) // Stored with the phase speed for menu snapshots.
#define AUTUMN_Q 64
#define AUTUMN_HALF_WIDTH 8
#define AUTUMN_ENTRY_X (-12)
#define AUTUMN_ENTRY_SPACING 32
#define tAutumnEntering data[4]
#define AUTUMN_LEFT (-24)
#define AUTUMN_RIGHT 264
#define AUTUMN_TOP (-24)
#define AUTUMN_BOTTOM 184
#define BLOOD_MOON_DRAIN_SPEED_STEP 12
#define BLOOD_MOON_DRAIN_ACCEL_FRAMES (7 * BLOOD_MOON_DRAIN_SPEED_STEP)

enum PetalMode
{
    PETAL_MODE_NONE,
    PETAL_MODE_SAKURA,
    PETAL_MODE_AUTUMN,
};

struct PetalSnapshot
{
    s16 data[PETALS_MAX_COUNT][8];
    s16 playerX;
    s16 playerY;
    s8 mapGroup;
    s8 mapNum;
    u8 mode;
    u8 count;
    u8 nextWeather;
    u8 drainMapGroup;
    u8 drainMapNum;
    bool8 draining;
    bool8 fastDrain;
    bool8 valid;
};

static EWRAM_DATA struct PetalSnapshot sSnapshot = {0};
static EWRAM_DATA u8 sSpriteIds[PETALS_MAX_COUNT] = {0};
static EWRAM_DATA u8 sLiveCount = 0;
static EWRAM_DATA u8 sMode = PETAL_MODE_NONE;
static EWRAM_DATA u8 sPaletteIndex = 0;
static EWRAM_DATA u16 sTileStart = 0;
static EWRAM_DATA u16 sGustPhase = 0;
static EWRAM_DATA bool8 sPaletteOwned = FALSE;
static EWRAM_DATA bool8 sTilesOwned = FALSE;
static EWRAM_DATA u8 sCopiedRampMask = 0;
static EWRAM_DATA bool8 sDraining = FALSE;
static EWRAM_DATA bool8 sFastDrain = FALSE;
static EWRAM_DATA bool8 sDrainMapValid = FALSE;
static EWRAM_DATA u8 sDrainMapGroup = 0;
static EWRAM_DATA u8 sDrainMapNum = 0;
static EWRAM_DATA bool8 sNeedsReplenish = FALSE;

static void Sakura_Update(struct Sprite *sprite);
static void AutumnWind_Update(struct Sprite *sprite);
static void Sakura_Draw(struct Sprite *sprite);
static void AutumnWind_Draw(struct Sprite *sprite);

static const u8 ALIGNED(4) sSakuraTiles[] = INCBIN_U8("graphics/weather/sakura_petals.4bpp");
static const u8 ALIGNED(4) sAutumnTiles[] = INCBIN_U8("graphics/weather/autumn_wind/tree_leaves.4bpp");

STATIC_ASSERT(sizeof(sSakuraTiles) == 9 * 128, SakuraNeedsNinePacked16x16Frames);
STATIC_ASSERT(sizeof(sAutumnTiles) == 16 * 128, AutumnNeedsSixteenPacked16x16Frames);
STATIC_ASSERT(KYOTO_AUTUMN_LEAF_COUNT <= PETALS_MAX_COUNT, AutumnSnapshotCapacity);
static const struct SpriteSheet sSheets[] =
{
    [PETAL_MODE_SAKURA] = {sSakuraTiles, sizeof(sSakuraTiles), PETALS_TILE_TAG},
    [PETAL_MODE_AUTUMN] = {sAutumnTiles, sizeof(sAutumnTiles), PETALS_TILE_TAG},
};

static const struct OamData sPetalOam =
{
    .affineMode = ST_OAM_AFFINE_OFF,
    .objMode = ST_OAM_OBJ_NORMAL,
    .bpp = ST_OAM_4BPP,
    .shape = SPRITE_SHAPE(16x16),
    .size = SPRITE_SIZE(16x16),
    .priority = 1, // BG0 dialogue, map names and the Start menu stay in front.
};

static const struct SpriteTemplate sSakuraTemplate =
{
    .tileTag = PETALS_TILE_TAG,
    .paletteTag = KYOTO_PETALS_PALETTE_TAG,
    .oam = &sPetalOam,
    .anims = gDummySpriteAnimTable,
    .images = NULL,
    .affineAnims = gDummySpriteAffineAnimTable,
    .callback = Sakura_Update,
};

static const struct SpriteTemplate sAutumnTemplate =
{
    .tileTag = PETALS_TILE_TAG,
    .paletteTag = KYOTO_PETALS_PALETTE_TAG,
    .oam = &sPetalOam,
    .anims = gDummySpriteAnimTable,
    .images = NULL,
    .affineAnims = gDummySpriteAffineAnimTable,
    .callback = AutumnWind_Update,
};

static bool8 OwnsSprite(u8 id)
{
    return id < MAX_SPRITES && gSprites[id].inUse
        && ((sMode == PETAL_MODE_SAKURA && gSprites[id].callback == Sakura_Update)
         || (sMode == PETAL_MODE_AUTUMN && gSprites[id].callback == AutumnWind_Update));
}

static u32 TargetParticleCount(void)
{
    return sMode == PETAL_MODE_SAKURA ? KYOTO_SAKURA_PETAL_COUNT : KYOTO_AUTUMN_LEAF_COUNT;
}

static void RemoveParticleSprite(struct Sprite *sprite)
{
    u8 id = sprite - gSprites;
    u32 i;

    for (i = 0; i < sLiveCount; i++)
    {
        if (sSpriteIds[i] == id)
        {
            sLiveCount--;
            sSpriteIds[i] = sSpriteIds[sLiveCount];
            break;
        }
    }
    DestroySprite(sprite);
}

static u16 PetalRandom(u16 *seed)
{
    // Cosmetic effects must not advance the battle/encounter random seed.
    *seed = (u16)(*seed * 109 + 89);
    return *seed;
}

static void RememberLocation(void)
{
    if (gSaveBlock1Ptr == NULL)
        return;
    sSnapshot.mapGroup = gSaveBlock1Ptr->location.mapGroup;
    sSnapshot.mapNum = gSaveBlock1Ptr->location.mapNum;
    sSnapshot.playerX = gSaveBlock1Ptr->pos.x;
    sSnapshot.playerY = gSaveBlock1Ptr->pos.y;
}

static bool8 CanResume(void)
{
    return sSnapshot.valid && sSnapshot.mode == sMode
        && sSnapshot.count <= PETALS_MAX_COUNT && gSaveBlock1Ptr != NULL
        && sSnapshot.mapGroup == gSaveBlock1Ptr->location.mapGroup
        && sSnapshot.mapNum == gSaveBlock1Ptr->location.mapNum
        && sSnapshot.playerX == gSaveBlock1Ptr->pos.x
        && sSnapshot.playerY == gSaveBlock1Ptr->pos.y;
}

static void RememberActiveMap(void)
{
    if (sDraining || gSaveBlock1Ptr == NULL)
        return;

    // Keep the last map on which this weather was still fully active. If the
    // connected-map load happens before the first Finish callback, this saved
    // origin still lets the outgoing particles recognize the destination map.
    sDrainMapGroup = gSaveBlock1Ptr->location.mapGroup;
    sDrainMapNum = gSaveBlock1Ptr->location.mapNum;
    sDrainMapValid = TRUE;
    sFastDrain = FALSE;
}

static void UpdateDrainMapState(void)
{
    if (!sDraining || gSaveBlock1Ptr == NULL)
        return;

    if (!sDrainMapValid)
    {
        sDrainMapGroup = gSaveBlock1Ptr->location.mapGroup;
        sDrainMapNum = gSaveBlock1Ptr->location.mapNum;
        sDrainMapValid = TRUE;
        return;
    }

    // Only accelerate once the player has actually entered a different map.
    // Before the boundary the source weather keeps its exact normal motion.
    if (sDrainMapGroup != gSaveBlock1Ptr->location.mapGroup
     || sDrainMapNum != gSaveBlock1Ptr->location.mapNum)
        sFastDrain = TRUE;
}

static s32 ScaleDrainMotion(s32 motion)
{
    if (gWeatherPtr->nextWeather == WEATHER_BLOOD_MOON)
        return motion + (motion * gWeatherPtr->finishStep) / BLOOD_MOON_DRAIN_SPEED_STEP;
    if (!sFastDrain)
        return motion;
    return (motion * KYOTO_PETALS_NEW_MAP_DRAIN_NUMERATOR)
         / KYOTO_PETALS_NEW_MAP_DRAIN_DENOMINATOR;
}

u8 Petals_GetResumeWeather(u8 targetWeather)
{
    // Saved weather already names the destination while the old particles
    // are draining out. Preserve that pending transition across a Bag/Party
    // visit instead of treating it as a fresh destination-weather load.
    if (CanResume() && sSnapshot.draining
     && sSnapshot.nextWeather == targetWeather)
    {
        if (sSnapshot.mode == PETAL_MODE_SAKURA)
            return WEATHER_SAKURA_PETALS;
        if (sSnapshot.mode == PETAL_MODE_AUTUMN)
            return WEATHER_AUTUMN_WIND;
    }
    return targetWeather;
}

static void ReleaseGraphics(void)
{
    if (sPaletteOwned)
    {
        if (IndexOfSpritePaletteTag(KYOTO_PETALS_PALETTE_TAG) == sPaletteIndex)
            FreeSpritePaletteByTag(KYOTO_PETALS_PALETTE_TAG);
        sPaletteOwned = FALSE;
    }
    if (sTilesOwned)
    {
        if (GetSpriteTileStartByTag(PETALS_TILE_TAG) == sTileStart)
            FreeSpriteTilesByTag(PETALS_TILE_TAG);
        sTilesOwned = FALSE;
    }
}

static void DestroyParticles(void)
{
    u32 i;
    for (i = 0; i < sLiveCount; i++)
        if (OwnsSprite(sSpriteIds[i]))
            DestroySprite(&gSprites[sSpriteIds[i]]);
    sLiveCount = 0;
    ReleaseGraphics();
}

void Petals_Reset(void)
{
    u32 i, j;
    if (!sLiveCount)
    {
        ReleaseGraphics();
        return;
    }

    sSnapshot.valid = TRUE;
    sSnapshot.mode = sMode;
    sSnapshot.count = sLiveCount;
    sSnapshot.nextWeather = gWeatherPtr->nextWeather;
    sSnapshot.drainMapGroup = sDrainMapGroup;
    sSnapshot.drainMapNum = sDrainMapNum;
    sSnapshot.draining = sDraining;
    sSnapshot.fastDrain = sFastDrain;
    RememberLocation();
    for (i = 0; i < sLiveCount; i++)
    {
        if (!OwnsSprite(sSpriteIds[i]))
        {
            sSnapshot.valid = FALSE;
            continue;
        }
        for (j = 0; j < 8; j++)
            sSnapshot.data[i][j] = gSprites[sSpriteIds[i]].data[j];
    }
    // This also runs on callback changes, which need not reset the whole
    // sprite pool immediately. Remove our old callbacks before freeing their
    // graphics; other sprites and the saved particle motion remain untouched.
    DestroyParticles();
}

// Each ramp maps colors in the OBJ palette to shades used by a General tree canopy.
struct CanopyRamp
{
    u8 mapPalette;
    u8 firstObjColor;
    u8 count;
    u8 mapColors[4];
};

static const struct CanopyRamp sSakuraRamp[] =
{
    {KYOTO_SAKURA_TREE_PALETTE, 1, 3, {KYOTO_SAKURA_TREE_LIGHT, KYOTO_SAKURA_TREE_MIDDLE, KYOTO_SAKURA_TREE_DARK}},
};

static const struct CanopyRamp sAutumnRamps[] =
{
    {2, 1, 3, {4, 6, 8}},       // red_leaf: green canopy, light to dark
    {3, 4, 4, {9, 10, 11, 12}}, // gold_leaf: pink canopy, light to dark
};

static const struct CanopyRamp *GetCanopyRamps(u32 *count)
{
    if (sMode == PETAL_MODE_SAKURA)
    {
        *count = ARRAY_COUNT(sSakuraRamp);
        return sSakuraRamp;
    }
    *count = ARRAY_COUNT(sAutumnRamps);
    return sAutumnRamps;
}

static bool8 LoadGraphics(void)
{
    u16 palette[16] = {0};
    struct SpritePalette spritePalette = {palette, KYOTO_PETALS_PALETTE_TAG};
    u32 i, j, count;
    const struct CanopyRamp *ramps = GetCanopyRamps(&count);

    sTileStart = LoadSpriteSheet(&sSheets[sMode]);
    // LoadSpriteSheet returns zero both for tile zero and allocation failure.
    if (GetSpriteTileStartByTag(PETALS_TILE_TAG) != sTileStart)
    {
        return FALSE;
    }
    sTilesOwned = TRUE;
    for (i = 0; i < count; i++)
        for (j = 0; j < ramps[i].count; j++)
            palette[ramps[i].firstObjColor + j] = gTileset_General.palettes[ramps[i].mapPalette][ramps[i].mapColors[j]];

    sPaletteIndex = LoadSpritePalette(&spritePalette);
    if (sPaletteIndex >= 16)
    {
        ReleaseGraphics();
        return FALSE;
    }
    sPaletteOwned = TRUE;
    sCopiedRampMask = 0;
    UpdateSpritePaletteWithWeather(sPaletteIndex, FALSE);
    return TRUE;
}

void Petals_BeforePaletteTransfer(void)
{
    u32 banks[ARRAY_COUNT(sAutumnRamps)];
    u32 i, j, count, mask = 0;
    const struct CanopyRamp *ramps;
    if (!sLiveCount || !sPaletteOwned || gMain.callback2 != CB2_Overworld
     || sPaletteIndex >= 16 || IndexOfSpritePaletteTag(KYOTO_PETALS_PALETTE_TAG) != sPaletteIndex)
        return;

    ramps = GetCanopyRamps(&count);
    for (i = 0; i < count; i++)
    {
        banks[i] = MAP_PALETTE_NONE;
        if (gMapHeader.mapLayout != NULL && gMapHeader.mapLayout->primaryTileset == &gTileset_General)
            banks[i] = MapPalettesGetPhysicalBank(ramps[i].mapPalette);
        if (banks[i] != MAP_PALETTE_NONE)
            mask |= 1u << i;
    }

    // Once a tree bank leaves the screen, restore the ordinary tinted OBJ colors.
    // During a fade the engine owns this transition; restore after the fade ends.
    if ((sCopiedRampMask & ~mask) && !gPaletteFade.active
     && gWeatherPtr->palProcessingState != WEATHER_PAL_STATE_SCREEN_FADING_IN
     && gWeatherPtr->palProcessingState != WEATHER_PAL_STATE_SCREEN_FADING_OUT)
    {
        UpdateSpritePaletteWithWeather(sPaletteIndex, FALSE);
        sCopiedRampMask = 0;
    }
    for (i = 0; i < count; i++)
        if (banks[i] != MAP_PALETTE_NONE)
            for (j = 0; j < ramps[i].count; j++)
                gPlttBufferFaded[OBJ_PLTT_ID(sPaletteIndex) + ramps[i].firstObjColor + j]
                    = gPlttBufferFaded[BG_PLTT_ID(banks[i]) + ramps[i].mapColors[j]];
    sCopiedRampMask |= mask;
}

static void LockPose(struct Sprite *sprite, u32 pose)
{
    sprite->oam.tileNum = sprite->sheetTileStart + pose * PETALS_FRAME_TILES;
    // Cancel the default frame-zero request before the first OAM build; this
    // prevents the old one-frame pose flash when returning from the Bag.
    sprite->animBeginning = FALSE;
    sprite->animEnded = TRUE;
    sprite->animPaused = TRUE;
    sprite->coordOffsetEnabled = FALSE;
}

static void Sakura_Draw(struct Sprite *sprite)
{
    u32 phase = (u16)sprite->data[2] >> 8;
    u32 pose = ((gSineTable[(phase + 64) & 255] + 256) * 8 + 255) >> 9;
    sprite->x = sprite->data[0];
    sprite->y = sprite->data[1] >> 7;
    sprite->x2 = (gSineTable[phase] * (sprite->data[5] & 15)) >> 8;
    sprite->y2 = 0;
    // Deeply negative OBJ coordinates wrap in hardware, so hide waiting petals.
    sprite->invisible = (sprite->data[4] & SAKURA_ENTERING)
                     && sprite->y <= -SAKURA_HALF_HEIGHT;
    LockPose(sprite, pose);
}

static void Sakura_Update(struct Sprite *sprite)
{
    s16 cameraX = gSpriteCoordOffsetX;
    s16 cameraY = gSpriteCoordOffsetY;
    bool8 entering = (sprite->data[4] & SAKURA_ENTERING) != 0;
    s16 dx = entering ? 0 : (s16)(cameraX - sprite->data[6]);
    s16 dy = entering ? 0 : (s16)(cameraY - sprite->data[7]);
    s32 x = sprite->data[0] + dx;
    s32 fall = sprite->data[3];
    s32 y;
    u16 seed = (u16)sprite->data[5] >> 4;

    UpdateDrainMapState();
    if (sDraining)
        fall = ScaleDrainMotion(fall);
    // Entry is relative to the screen's top edge, even while the player walks.
    y = sprite->data[1] + dy * 128 + fall;

    if (sDraining)
    {
        // Retire each outgoing petal at the edge while keeping its normal sway.
        if (y > 176 * 128 || y < -16 * 128 || x < -24 || x > 264)
        {
            RemoveParticleSprite(sprite);
            return;
        }
    }
    else
    {
        if (y > 176 * 128)
        {
            y -= 192 * 128;
            x = ((u32)PetalRandom(&seed) * 240) >> 16;
            sprite->data[5] = (s16)(((seed & 0xFFF) << 4) | (sprite->data[5] & 15));
        }
        if (!entering && y < -16 * 128)
            y += 192 * 128;
        if (x < -24)
            x += 288;
        if (x > 264)
            x -= 288;
    }

    sprite->data[0] = x;
    sprite->data[1] = y;
    sprite->data[2] = (u16)(sprite->data[2] + (sprite->data[4] & ~SAKURA_ENTERING));
    if (entering && y >= SAKURA_HALF_HEIGHT * 128)
        sprite->data[4] &= ~SAKURA_ENTERING;
    sprite->data[6] = cameraX;
    sprite->data[7] = cameraY;
    Sakura_Draw(sprite);
}

static s16 WrapAutumn(s32 position, s16 low, s16 high)
{
    // Peach restarts a leaf exactly on the opposite boundary.
    if (position > high * AUTUMN_Q)
        return low * AUTUMN_Q;
    if (position < low * AUTUMN_Q)
        return high * AUTUMN_Q;
    return position;
}

static void AutumnWind_Draw(struct Sprite *sprite)
{
    u16 frame = ((sprite->data[3] >> 3) & 7) + (sprite->data[2] & 1) * 8;
    sprite->x = sprite->data[0] >> 6;
    sprite->y = sprite->data[1] >> 6;
    sprite->x2 = 0;
    sprite->y2 = gSineTable[sprite->data[3] & 255] >> 4;
    sprite->invisible = sprite->tAutumnEntering && sprite->x <= -AUTUMN_HALF_WIDTH;
    LockPose(sprite, frame);
}

static void AutumnWind_Update(struct Sprite *sprite)
{
    s16 dx = (s16)(gSpriteCoordOffsetX - sprite->data[6]);
    s16 dy = (s16)(gSpriteCoordOffsetY - sprite->data[7]);
    s16 wave;
    s32 motionX;
    s32 motionY;
    s32 x;
    s32 y;

    sprite->data[6] = gSpriteCoordOffsetX;
    sprite->data[7] = gSpriteCoordOffsetY;
    sprite->data[3] = (sprite->data[3] + 1) & 511;
    wave = gSineTable[sprite->data[3] & 255];
    UpdateDrainMapState();
    // While entering, move relative to the screen's left edge even if the player runs.
    if (sprite->tAutumnEntering)
    {
        dx = 0;
        dy = 0;
        if (sDraining && sprite->data[0] <= -AUTUMN_HALF_WIDTH * AUTUMN_Q)
        {
            RemoveParticleSprite(sprite);
            return;
        }
    }
    motionX = 55 + (wave >> 4);
    motionY = -5;
    if (sDraining)
    {
        motionX = ScaleDrainMotion(motionX);
        motionY = ScaleDrainMotion(motionY);
    }
    x = sprite->data[0] + (s32)dx * AUTUMN_Q + motionX;
    y = sprite->data[1] + (s32)dy * AUTUMN_Q + motionY;

    if (sDraining)
    {
        // Keep the exact normal wind/wave equation, but do not wrap a leaf back
        // onto the opposite edge once the weather is being retired.
        if (x < AUTUMN_LEFT * AUTUMN_Q || x > AUTUMN_RIGHT * AUTUMN_Q
         || y < AUTUMN_TOP * AUTUMN_Q || y > AUTUMN_BOTTOM * AUTUMN_Q)
        {
            RemoveParticleSprite(sprite);
            return;
        }
        sprite->data[0] = x;
        sprite->data[1] = y;
    }
    else
    {
        // Starts can lie beyond the usual wrap boundary. Let them travel in from there.
        sprite->data[0] = sprite->tAutumnEntering ? x : WrapAutumn(x, AUTUMN_LEFT, AUTUMN_RIGHT);
        sprite->data[1] = WrapAutumn(y, AUTUMN_TOP, AUTUMN_BOTTOM);
    }
    if (sprite->tAutumnEntering && x >= AUTUMN_HALF_WIDTH * AUTUMN_Q)
        sprite->tAutumnEntering = FALSE;
    AutumnWind_Draw(sprite);
}

static void InitFreshParticle(struct Sprite *sprite, u32 index, u16 *seed, bool8 offscreen)
{
    if (sMode == PETAL_MODE_SAKURA)
    {
        sprite->data[0] = ((u32)PetalRandom(seed) * 240) >> 16;
        // A fresh map starts empty; stagger the population above the screen.
        sprite->data[1] = (SAKURA_ENTRY_Y - (s32)index * SAKURA_ENTRY_SPACING) * 128;
        sprite->data[2] = PetalRandom(seed);
        sprite->data[3] = 18 + (PetalRandom(seed) & 7);
        sprite->data[4] = SAKURA_ENTERING | (256 + (PetalRandom(seed) & 127));
        sprite->data[5] = (s16)(((PetalRandom(seed) & 0xFFF) << 4) | (9 + (index & 3)));
    }
    else
    {
        sprite->data[0] = (AUTUMN_ENTRY_X - (s32)index * AUTUMN_ENTRY_SPACING) * AUTUMN_Q;
        sprite->tAutumnEntering = TRUE;
        if (offscreen)
            sprite->data[1] = (16 + (PetalRandom(seed) & 127)) * AUTUMN_Q;
        else
            sprite->data[1] = (((index * 29) & 127) + 16) * AUTUMN_Q;
        sprite->data[2] = index & 1;
        sprite->data[3] = index * 37;
    }

    sprite->data[6] = gSpriteCoordOffsetX;
    sprite->data[7] = gSpriteCoordOffsetY;
    if (sMode == PETAL_MODE_SAKURA)
        Sakura_Draw(sprite);
    else
        AutumnWind_Draw(sprite);
}

static bool8 AddFreshParticle(u32 index, u16 *seed, bool8 offscreen)
{
    const struct SpriteTemplate *template = sMode == PETAL_MODE_SAKURA ? &sSakuraTemplate : &sAutumnTemplate;
    u8 id = CreateSpriteAtEnd(template, 0, 0, 78);

    if (id >= MAX_SPRITES)
        return FALSE;
    sSpriteIds[sLiveCount++] = id;
    InitFreshParticle(&gSprites[id], index, seed, offscreen);
    return TRUE;
}

static void ReplenishParticles(void)
{
    u16 seed = 0x517B ^ sGustPhase ^ (sLiveCount * 0x31);
    u32 target = TargetParticleCount();

    if (sDraining)
        return;
    while (sLiveCount < target)
    {
        if (!AddFreshParticle(sLiveCount, &seed, TRUE))
            break;
    }
    sNeedsReplenish = FALSE;
}

static void CreateParticles(void)
{
    u32 i, j;
    u16 seed = 0x517B;
    u32 target = TargetParticleCount();
    u32 count;
    bool8 resume = CanResume();

    if (sLiveCount)
        return;

    sDraining = resume ? sSnapshot.draining : FALSE;
    sFastDrain = resume ? sSnapshot.fastDrain : FALSE;
    sDrainMapValid = resume ? TRUE : FALSE;
    if (resume)
    {
        sDrainMapGroup = sSnapshot.drainMapGroup;
        sDrainMapNum = sSnapshot.drainMapNum;
    }
    count = resume ? sSnapshot.count : target;
    if (!LoadGraphics())
        return;

    for (i = 0; i < count; i++)
    {
        const struct SpriteTemplate *template = sMode == PETAL_MODE_SAKURA ? &sSakuraTemplate : &sAutumnTemplate;
        u8 id = CreateSpriteAtEnd(template, 0, 0, 78);
        struct Sprite *sprite;

        if (id >= MAX_SPRITES)
            break;
        sprite = &gSprites[id];
        sSpriteIds[sLiveCount++] = id;
        if (resume)
        {
            for (j = 0; j < 8; j++)
                sprite->data[j] = sSnapshot.data[i][j];

            // Menu reconstruction resets the camera origin. Preserve position
            // and pose, but rebase the camera delta before the first update.
            sprite->data[6] = gSpriteCoordOffsetX;
            sprite->data[7] = gSpriteCoordOffsetY;
            if (sMode == PETAL_MODE_SAKURA)
                Sakura_Draw(sprite);
            else
                AutumnWind_Draw(sprite);
        }
        else
        {
            InitFreshParticle(sprite, i, &seed, FALSE);
        }
    }

    if (!sLiveCount)
        ReleaseGraphics();
    sSnapshot.valid = FALSE;
    RememberActiveMap();
    RememberLocation();
}

static void InitParticles(u8 mode)
{
    if (sLiveCount && sMode != mode)
        DestroyParticles();

    // Native SetNextWeather calls initVars when a transition is cancelled by
    // walking back into the still-current weather. Stop retiring survivors and
    // refill only the missing density from the normal offscreen entry edge.
    if (sMode == mode && sDraining)
    {
        sDraining = FALSE;
        sFastDrain = FALSE;
        sNeedsReplenish = TRUE;
    }

    sMode = mode;
    gWeatherPtr->initStep = 0;
    gWeatherPtr->weatherGfxLoaded = FALSE;
    gWeatherPtr->targetColorMapIndex = 0;
    gWeatherPtr->colorMapStepDelay = 20;
    Weather_SetBlendCoeffs(8, BASE_SHADOW_INTENSITY);
    gWeatherPtr->noShadows = FALSE;
}

static void RunParticles(void)
{
    if (!gWeatherPtr->initStep)
    {
        // A cancelled drain can reach zero particles one frame before the
        // weather task notices. If its graphics are still owned, repopulate
        // from the entry edge instead of popping a full field into existence.
        if (!sLiveCount && sNeedsReplenish && sTilesOwned && sPaletteOwned)
        {
            // Resources are already present; ReplenishParticles below will
            // create only offscreen replacements.
        }
        else if (!sLiveCount)
        {
            CreateParticles();
        }

        // Allocation failure must never trap the weather initialization loop.
        gWeatherPtr->initStep = 1;
        gWeatherPtr->weatherGfxLoaded = TRUE;
    }

    if (sNeedsReplenish)
        ReplenishParticles();

    RememberActiveMap();
    sGustPhase += 64;
    RememberLocation();
}

static bool8 FinishParticles(void)
{
    if (!sLiveCount && CanResume())
    {
        CreateParticles();
        if (sLiveCount)
            return TRUE;
    }

    if (sLiveCount)
    {
        // No dithered tile mask and no global BLDALPHA change. Existing
        // Particles stop recycling and move offscreen. The weekly transition
        // ramps up the outgoing motion so the magenta fade can start promptly.
        if (!sDraining)
        {
            if (!sDrainMapValid && gSaveBlock1Ptr != NULL)
            {
                sDrainMapGroup = gSaveBlock1Ptr->location.mapGroup;
                sDrainMapNum = gSaveBlock1Ptr->location.mapNum;
                sDrainMapValid = TRUE;
            }
            sFastDrain = FALSE;
            sDraining = TRUE;
        }
        UpdateDrainMapState();
        if (gWeatherPtr->nextWeather == WEATHER_BLOOD_MOON && gWeatherPtr->finishStep < BLOOD_MOON_DRAIN_ACCEL_FRAMES)
            gWeatherPtr->finishStep++;
        sNeedsReplenish = FALSE;
        sGustPhase += 64;
        RememberLocation();
        return TRUE;
    }

    ReleaseGraphics();
    sSnapshot.valid = FALSE;
    sSnapshot.count = 0;
    sMode = PETAL_MODE_NONE;
    sDraining = FALSE;
    sFastDrain = FALSE;
    sDrainMapValid = FALSE;
    sNeedsReplenish = FALSE;
    gWeatherPtr->weatherGfxLoaded = FALSE;
    return FALSE;
}

void Sakura_InitVars(void) { InitParticles(PETAL_MODE_SAKURA); }
void Sakura_Main(void) { RunParticles(); }
void Sakura_InitAll(void) { Sakura_InitVars(); Sakura_Main(); }
bool8 Sakura_Finish(void) { return FinishParticles(); }
void AutumnWind_InitVars(void) { InitParticles(PETAL_MODE_AUTUMN); }
void AutumnWind_Main(void) { RunParticles(); }
void AutumnWind_InitAll(void) { AutumnWind_InitVars(); AutumnWind_Main(); }
bool8 AutumnWind_Finish(void) { return FinishParticles(); }
