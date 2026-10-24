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
// Autumn Wind retains the original gold/red leaf artwork and eight poses each.
#define PETALS_TILE_TAG 0x1210
#define PETALS_FRAME_TILES 4
#define PETALS_MAX_COUNT KYOTO_SAKURA_PETAL_COUNT
#define AUTUMN_Q 64
#define AUTUMN_LEFT (-24)
#define AUTUMN_RIGHT 264
#define AUTUMN_TOP (-24)
#define AUTUMN_BOTTOM 184

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
static EWRAM_DATA bool8 sCopiedTreeColors = FALSE;
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
static const u8 ALIGNED(4) sAutumnTiles[] = INCBIN_U8("graphics/weather/autumn_wind/leaves.4bpp");
static const u16 sAutumnPalette[] = INCBIN_U16("graphics/weather/autumn_wind/leaves.gbapal");

STATIC_ASSERT(sizeof(sSakuraTiles) == 9 * 128, SakuraNeedsNinePacked16x16Frames);
STATIC_ASSERT(sizeof(sAutumnTiles) == 16 * 128, AutumnNeedsSixteenPacked16x16Frames);
STATIC_ASSERT(sizeof(sAutumnPalette) == 16 * sizeof(u16), AutumnNeedsOne4bppPalette);
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

static bool8 LoadGraphics(void)
{
    u16 palette[16] = {0};
    struct SpritePalette spritePalette = {palette, KYOTO_PETALS_PALETTE_TAG};
    u32 i;

    sTileStart = LoadSpriteSheet(&sSheets[sMode]);
    // LoadSpriteSheet returns zero both for tile zero and allocation failure.
    if (GetSpriteTileStartByTag(PETALS_TILE_TAG) != sTileStart)
    {
        return FALSE;
    }
    sTilesOwned = TRUE;
    if (sMode == PETAL_MODE_SAKURA)
    {
        // Use the authored tree colors as the unfaded OBJ source. The live
        // palette copy below follows day/night and ordinary screen fades.
        palette[1] = gTileset_General.palettes[KYOTO_SAKURA_TREE_PALETTE][KYOTO_SAKURA_TREE_LIGHT];
        palette[2] = gTileset_General.palettes[KYOTO_SAKURA_TREE_PALETTE][KYOTO_SAKURA_TREE_MIDDLE];
        palette[3] = gTileset_General.palettes[KYOTO_SAKURA_TREE_PALETTE][KYOTO_SAKURA_TREE_DARK];
    }
    else
    {
        for (i = 0; i < 16; i++)
            palette[i] = sAutumnPalette[i];
    }

    sPaletteIndex = LoadSpritePalette(&spritePalette);
    if (sPaletteIndex >= 16)
    {
        ReleaseGraphics();
        return FALSE;
    }
    sPaletteOwned = TRUE;
    sCopiedTreeColors = FALSE;
    UpdateSpritePaletteWithWeather(sPaletteIndex, FALSE);
    return TRUE;
}

void Petals_BeforePaletteTransfer(void)
{
    u32 bank, offset;
    if (!sLiveCount || gMain.callback2 != CB2_Overworld)
        return;


    if (!sPaletteOwned || sMode != PETAL_MODE_SAKURA
     || gMapHeader.mapLayout == NULL
     || gMapHeader.mapLayout->primaryTileset != &gTileset_General
     || sPaletteIndex >= 16
     || IndexOfSpritePaletteTag(KYOTO_PETALS_PALETTE_TAG) != sPaletteIndex)
        return;

    bank = MapPalettesGetPhysicalBank(KYOTO_SAKURA_TREE_PALETTE);
    if (bank == MAP_PALETTE_NONE)
    {
        // Once the tree bank leaves the screen, return to the normal OBJ
        // result. Only refresh when idle so native screen fades remain in
        // control of the palette pipeline.
        if (sCopiedTreeColors && !gPaletteFade.active
         && gWeatherPtr->palProcessingState != WEATHER_PAL_STATE_SCREEN_FADING_IN
         && gWeatherPtr->palProcessingState != WEATHER_PAL_STATE_SCREEN_FADING_OUT)
        {
            UpdateSpritePaletteWithWeather(sPaletteIndex, FALSE);
            sCopiedTreeColors = FALSE;
        }
        return;
    }

    // Read the logical tree palette's CURRENT bank after every palette effect.
    // This follows remapping, morning/evening interpolation and screen fades
    // exactly, without reserving an additional BG bank or double-tinting.
    offset = OBJ_PLTT_ID(sPaletteIndex);
    gPlttBufferFaded[offset + 1] = gPlttBufferFaded[BG_PLTT_ID(bank) + KYOTO_SAKURA_TREE_LIGHT];
    gPlttBufferFaded[offset + 2] = gPlttBufferFaded[BG_PLTT_ID(bank) + KYOTO_SAKURA_TREE_MIDDLE];
    gPlttBufferFaded[offset + 3] = gPlttBufferFaded[BG_PLTT_ID(bank) + KYOTO_SAKURA_TREE_DARK];
    sCopiedTreeColors = TRUE;
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
    LockPose(sprite, pose);
}

static void Sakura_Update(struct Sprite *sprite)
{
    s16 cameraX = gSpriteCoordOffsetX;
    s16 cameraY = gSpriteCoordOffsetY;
    s32 x = sprite->data[0] + (s16)(cameraX - sprite->data[6]);
    s32 fall = sprite->data[3];
    s32 y;
    u16 seed = (u16)sprite->data[5] >> 4;

    UpdateDrainMapState();
    if (sDraining)
        fall = ScaleDrainMotion(fall);
    y = sprite->data[1] + (s16)(cameraY - sprite->data[7]) * 128 + fall;

    if (sDraining)
    {
        // During an outgoing transition, never recycle a petal. It keeps its
        // normal sway and source-map speed; after the map ID changes only the
        // fall speed is boosted. The existing cell is then retired at the edge.
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
        if (y < -16 * 128)
            y += 192 * 128;
        if (x < -24)
            x += 288;
        if (x > 264)
            x -= 288;
    }

    sprite->data[0] = x;
    sprite->data[1] = y;
    sprite->data[2] = (u16)(sprite->data[2] + sprite->data[4]);
    sprite->data[6] = cameraX;
    sprite->data[7] = cameraY;
    Sakura_Draw(sprite);
}

static s16 WrapAutumn(s32 position, s16 low, s16 high)
{
    // Preserve overshoot to avoid a small pause at the wrapping boundary.
    if (position > high * AUTUMN_Q)
        position -= (high - low) * AUTUMN_Q;
    if (position < low * AUTUMN_Q)
        position += (high - low) * AUTUMN_Q;
    return position;
}

static void AutumnWind_Draw(struct Sprite *sprite)
{
    u16 frame = ((sprite->data[3] >> 3) & 7) + (sprite->data[2] & 1) * 8;
    sprite->x = sprite->data[0] >> 6;
    sprite->y = sprite->data[1] >> 6;
    sprite->x2 = 0;
    sprite->y2 = gSineTable[sprite->data[3] & 255] >> 4;
    LockPose(sprite, frame);
}

static void AutumnWind_Update(struct Sprite *sprite)
{
    s16 dx = (s16)(gSpriteCoordOffsetX - sprite->data[6]);
    s16 dy = (s16)(gSpriteCoordOffsetY - sprite->data[7]);
    s16 wave;
    s16 gust = (gSineTable[(sGustPhase >> 8) & 255] + 256) >> 5;
    s32 motionX;
    s32 motionY;
    s32 x;
    s32 y;

    sprite->data[6] = gSpriteCoordOffsetX;
    sprite->data[7] = gSpriteCoordOffsetY;
    sprite->data[3] = (sprite->data[3] + 1) & 511;
    wave = gSineTable[sprite->data[3] & 255];
    UpdateDrainMapState();
    motionX = -47 - gust + (wave >> 4);
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
        sprite->data[0] = WrapAutumn(x, AUTUMN_LEFT, AUTUMN_RIGHT);
        sprite->data[1] = WrapAutumn(y, AUTUMN_TOP, AUTUMN_BOTTOM);
    }
    AutumnWind_Draw(sprite);
}

static void InitFreshParticle(struct Sprite *sprite, u32 index, u16 *seed, bool8 offscreen)
{
    if (sMode == PETAL_MODE_SAKURA)
    {
        sprite->data[0] = ((u32)PetalRandom(seed) * 240) >> 16;
        if (offscreen)
            sprite->data[1] = -16 * 128;
        else
            sprite->data[1] = (s16)((((u32)PetalRandom(seed) * 192) >> 16) - 16) * 128;
        sprite->data[2] = PetalRandom(seed);
        sprite->data[3] = 18 + (PetalRandom(seed) & 7);
        sprite->data[4] = 256 + (PetalRandom(seed) & 127);
        sprite->data[5] = (s16)(((PetalRandom(seed) & 0xFFF) << 4) | (9 + (index & 3)));
    }
    else
    {
        sprite->data[0] = (offscreen ? AUTUMN_RIGHT : (((index * 47) & 255) - 8)) * AUTUMN_Q;
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
        // particles stop recycling. They keep normal speed on the source map;
        // after a real map change their motion is mildly accelerated so the
        // outgoing weather clears the destination sooner.
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
