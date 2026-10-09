#include "global.h"
#include "field_camera.h"
#include "field_weather.h"
#include "main.h"
#include "malloc.h"
#include "overworld.h"
#include "palette.h"
#include "sprite.h"
#include "util.h"
#include "constants/weather.h"
#include "kyoto_cloud_layers.h"
#include "kyoto_cloud_config.h"
#include "gpu_regs.h"

#define CLOUD_COUNT 11
#define CLOUD_CELL_BYTES 2048
#define CLOUD_OBJ_BYTES (CLOUD_COUNT * CLOUD_CELL_BYTES)
#define ALL_CLOUD_CELLS ((1 << CLOUD_COUNT) - 1)
static const u8 sCloudTiles[] = INCBIN_U8("graphics/weather/kyoto_clouds/foreground.bin");
static const u16 sCloudPalette[] = INCBIN_U16("graphics/weather/kyoto_clouds/foreground_palette.bin");
extern const u32 KyotoCloudShadePixels[], KyotoCloudShadePixelsEnd[];

struct CloudState
{
    u8 *buffer;
    int shadowX[11], shadowY[11], shadowArgs[11];
    u8 tones[16];
    u8 sprites[CLOUD_COUNT];
    bool8 created, paused, fieldLoading, holdBlack;
    u16 oldShadows, dirtyCells;
    u16 lastX, lastY;
    s32 cameraX, cameraY;
    u32 tickBase, pauseFrame, pauseTickBase;
};
EWRAM_DATA static struct CloudState sCloud = {0};
static bool8 sShadowWindowActive;

bool32 KyotoCloud_UsesShadowWindow(void)
{
    // Hardware fades and iris/flash windows retain their normal ownership.
    return sCloud.created && gMain.callback2 == CB2_Overworld
        && !(gPaletteFade.active && gPaletteFade.mode == 2 /* HARDWARE_FADE */)
        && GetGpuReg(REG_OFFSET_WIN0H) == 0x00FF
        && GetGpuReg(REG_OFFSET_WIN0V) == 0x00FF
        && GetGpuReg(REG_OFFSET_WIN1V) == 0xFFFF;
}

static void UpdateShadowWindowVBlank(void)
{
    if (KyotoCloud_UsesShadowWindow())
    {
        // Semi-transparent cloud OBJs always use BLDALPHA. In contrast,
        // shadow OBJ windows darken only the BG underneath their pixel mask.
        // Do not alter the register manager: menus, fades and other weather
        // must retain their own saved register values.
        REG_DISPCNT = (GetGpuReg(REG_OFFSET_DISPCNT) & ~(DISPCNT_WIN0_ON | DISPCNT_WIN1_ON)) | DISPCNT_OBJWIN_ON;
        REG_WINOUT = 0x3F1F; // all layers; brightness only inside a shadow
        REG_BLDCNT = (GetGpuReg(REG_OFFSET_BLDCNT) & BLDCNT_TGT2_ALL)
                   | BLDCNT_EFFECT_DARKEN | BLDCNT_TGT1_BG1 | BLDCNT_TGT1_BG2 | BLDCNT_TGT1_BG3;
        REG_BLDY = OW_SHADOW_INTENSITY;
        sShadowWindowActive = TRUE;
    }
    else if (sShadowWindowActive)
    {
        REG_DISPCNT = GetGpuReg(REG_OFFSET_DISPCNT);
        REG_WINOUT = GetGpuReg(REG_OFFSET_WINOUT);
        REG_BLDCNT = GetGpuReg(REG_OFFSET_BLDCNT);
        REG_BLDY = GetGpuReg(REG_OFFSET_BLDY);
        sShadowWindowActive = FALSE;
    }
}

static void CloudSpriteCallback(struct Sprite *sprite);
static const struct OamData sCloudOam =
{
    .affineMode = ST_OAM_AFFINE_OFF,
    .objMode = ST_OAM_OBJ_BLEND,
    .bpp = ST_OAM_4BPP,
    .shape = SPRITE_SHAPE(64x64),
    .size = SPRITE_SIZE(64x64),
    .priority = 1,
};
static const union AnimCmd sCloudAnim[] = { ANIMCMD_FRAME(0, 16), ANIMCMD_END };
static const union AnimCmd *const sCloudAnims[] = { sCloudAnim };
static const struct SpriteTemplate sCloudTemplate =
{
    .tileTag = GFXTAG_CLOUD,
    .paletteTag = PALTAG_WEATHER_2,
    .oam = &sCloudOam,
    .anims = sCloudAnims,
    .images = NULL,
    .affineAnims = gDummySpriteAffineAnimTable,
    .callback = CloudSpriteCallback,
};

static void UpdateCamera(void)
{
    s16 x, y;
    GetCameraOffsetWithPan(&x, &y);
    sCloud.cameraX += (s8)(x - sCloud.lastX);
    sCloud.cameraY += (s8)(y - sCloud.lastY);
    sCloud.lastX = x;
    sCloud.lastY = y;
}

static void FreeBuffer(void)
{
    sCloud.dirtyCells = 0;
    if (sCloud.buffer != NULL)
        Free(sCloud.buffer);
    sCloud.buffer = NULL;
}

void KyotoCloud_Create(void)
{
    u32 i;
    s16 x, y;
    struct SpriteSheet sheet;
    if (gWeatherPtr->cloudSpritesCreated)
        return;
    sheet.data = sCloudTiles;
    sheet.size = sizeof(sCloudTiles);
    sheet.tag = GFXTAG_CLOUD;
    LoadSpriteSheet(&sheet);
    if (GetSpriteTileStartByTag(GFXTAG_CLOUD) == TAG_NONE)
    {
        gWeatherPtr->cloudSpritesCreated = TRUE;
        return; // No OBJ allocation: leave the rest of the field intact.
    }
    LoadCustomWeatherSpritePalette(sCloudPalette);
    for (i = 0; i < CLOUD_COUNT; i++)
    {
        u8 id = CreateSpriteUnchecked(&sCloudTemplate, 0, 0, 0);
        sCloud.sprites[i] = id;
        if (id != MAX_SPRITES)
        {
            gSprites[id].data[0] = i;
            gSprites[id].sheetTileStart += i * 64;
            gSprites[id].oam.tileNum = gSprites[id].sheetTileStart;
            gSprites[id].coordOffsetEnabled = FALSE;
            gSprites[id].animPaused = TRUE;
        }
    }
    GetCameraOffsetWithPan(&x, &y);
    sCloud.lastX = x;
    sCloud.lastY = y;
    sCloud.cameraX = sCloud.cameraY = 0;
    sCloud.tickBase = gMain.vblankCounter1;
    sCloud.created = TRUE;
    gWeatherPtr->cloudSpritesCreated = TRUE;
}

void KyotoCloud_Destroy(void)
{
    u32 i;
    for (i = 0; i < CLOUD_COUNT; i++)
    {
        u8 id = sCloud.sprites[i];
        if (sCloud.created && id < MAX_SPRITES && gSprites[id].inUse && gSprites[id].callback == CloudSpriteCallback)
            DestroySprite(&gSprites[id]);
    }
    FreeSpriteTilesByTag(GFXTAG_CLOUD);
    FreeBuffer();
    memset(&sCloud, 0, sizeof(sCloud));
    gWeatherPtr->cloudSpritesCreated = FALSE;
}

static void CloudSpriteCallback(struct Sprite *sprite)
{
    u32 tick, index = sprite->data[0];
    s32 x, y, lane;
    if (sCloud.paused)
    {
        sprite->invisible = TRUE;
        return;
    }
    UpdateCamera();
    tick = (gMain.vblankCounter1 - sCloud.tickBase) & 4095;
    // Each off-screen horizontal wrap advances one world lane. Keep camera
    // subtraction so walking never drags the weather along with the player.
    if (index < 8)
    {
        x = 256 - (s32)tick - sCloud.cameraX;
        lane = ((x >> 9) & 3) * 64;
        x = (x & 511) - 224;
        y = ((160 + lane - sCloud.cameraY) & 255) - 96;
        x += (index & 3) * 64;
        y += (index / 4) * 64;
    }
    else if (index < 10)
    {
        x = 416 - (s32)tick - sCloud.cameraX;
        lane = ((x >> 9) & 3) * 64;
        x = (x & 511) - 96;
        y = ((224 + lane - sCloud.cameraY) & 255) - 32;
        x += (index - 8) * 64;
    }
    else
    {
        x = 496 - (s32)tick - sCloud.cameraX;
        lane = ((x >> 9) & 3) * 64;
        x = (x & 511) - 32;
        y = ((32 + lane - sCloud.cameraY) & 255) - 32;
    }
    sprite->x = x;
    sprite->y = y;
    sprite->x2 = sprite->y2 = 0;
    sprite->oam.priority = 1;
    sprite->subpriority = 0;
    sprite->oam.tileNum = sprite->sheetTileStart;
    sprite->invisible = (x <= -32 || x >= 272 || y <= -32 || y >= 192);
}

void KyotoCloud_OnCallbackChange(void (*callback)(void))
{
    if (sCloud.created && gMain.callback2 == CB2_Overworld && callback != CB2_Overworld)
    {
        sCloud.paused = TRUE;
        sCloud.pauseFrame = gMain.vblankCounter1;
        sCloud.pauseTickBase = sCloud.tickBase;
        sCloud.holdBlack = TRUE;
        sCloud.fieldLoading = FALSE;
        sCloud.created = FALSE; // Wait for weather sprite recreation before touching new field VRAM.
        FreeBuffer();
    }
}

void KyotoCloud_SetFieldLoading(void)
{
    // Every map rebuild needs this guard, including clear-weather maps.
    // Start-menu reconstruction spans several frames before its fade starts.
    sCloud.fieldLoading = TRUE;
}

void KyotoCloud_BeforePaletteTransfer(void)
{
    // UpdatePaletteFade clears the selection mask before the field callback
    // hands control to Bag. Field reconstruction must remain black until ready.
    if (sCloud.fieldLoading
     || (sCloud.created && gMain.callback2 == CB2_Overworld
      && gPaletteFade.y == 16 && gPaletteFade.blendColor == 0
      && (gPaletteFadeSelectedPalettes == 0
       || gPaletteFadeSelectedPalettes == PALETTES_ALL)))
        CpuFill16(0, gPlttBufferFaded, PLTT_SIZE);
}

static inline __attribute__((always_inline)) int OamX(u16 b) { int x = b & 511; return x >= 256 ? x - 512 : x; }
static inline __attribute__((always_inline)) int OamY(u16 a) { int y = a & 255; return y >= 160 ? y - 256 : y; }
static const u8 sWidths[] = {8,16,32,64,16,32,32,64,8,8,16,32};
static const u8 sHeights[] = {8,16,32,64,8,8,16,32,16,32,32,64};

static void PrepareShadows(u32 *code)
{
    u32 i, j, k, base = GetSpriteTileStartByTag(GFXTAG_CLOUD), present = 0, touched = 0;
    int *cloudX = sCloud.shadowX, *cloudY = sCloud.shadowY;
    const u16 *oam = (const u16 *)gMain.oamBuffer;
    u8 *buffer = sCloud.buffer;
    u32 (*shade)(u8 *, const u8 *, const u8 *, const int *) = (void *)code;
    for (j = 0; j < CLOUD_COUNT; j++)
        if (sCloud.oldShadows & (1 << j))
            DmaCopy32(3, sCloudTiles + j * CLOUD_CELL_BYTES, buffer + j * CLOUD_CELL_BYTES, CLOUD_CELL_BYTES);
    for (i = 0; i < 128; i++)
    {
        u16 a = oam[i * 4], b = oam[i * 4 + 1], c = oam[i * 4 + 2];
        u32 tile = c & 1023;
        if ((a & 0x300) == 0x200 || tile < base || tile >= base + 704 || ((tile - base) & 63))
            continue;
        j = (tile - base) >> 6;
        cloudX[j] = OamX(b);
        cloudY[j] = OamY(a);
        present |= 1 << j;
    }
    for (i = 0; i < 128; i++)
    {
        u16 a = oam[i * 4], b = oam[i * 4 + 1], c = oam[i * 4 + 2];
        u32 tile = c & 1023, shape, dim, width, height;
        int x, y;
        u8 *tones = sCloud.tones;
        const u8 *pixels;
        const u16 *palette;
        // Native unused OAM entries have y=160 and cannot reach the screen.
        // Ground-shadow/blend sprites do not cast another silhouette.
        if (a == DISPLAY_HEIGHT || (a & 0x2F00) || !(c & 0xC00) || (tile >= base && tile < base + 704))
            continue;
        shape = a >> 14;
        if (shape == 3)
            continue;
        dim = shape * 4 + (b >> 14);
        width = sWidths[dim]; height = sHeights[dim]; x = OamX(b); y = OamY(a);
        if (x >= 240 || y >= 160 || x + (int)width <= 0 || y + (int)height <= 0)
            continue;
        pixels = GetSpriteGraphicsForPendingFrame((const u8 *)(OBJ_VRAM0 + tile * 32), width * height / 2);
        palette = &gPlttBufferFaded[OBJ_PLTT_OFFSET + (c >> 12) * 16];
        for (k = 0; k < 16; k++)
        {
            u16 color = palette[k];
            u32 lum = (color & 31) + 2 * ((color >> 5) & 31) + ((color >> 10) & 31);
            tones[k] = lum < 40 ? 0 : lum < 80 ? 1 : 2;
        }
        for (j = 0; j < CLOUD_COUNT; j++)
        {
            int x0, y0, x1, y1;
            int *args = sCloud.shadowArgs;
            if (!(present & (1 << j))) continue;
            x0 = x > cloudX[j] ? x : cloudX[j]; y0 = y > cloudY[j] ? y : cloudY[j];
            x1 = x + (int)width < cloudX[j] + 64 ? x + (int)width : cloudX[j] + 64;
            y1 = y + (int)height < cloudY[j] + 64 ? y + (int)height : cloudY[j] + 64;
            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            if (x1 > 240) x1 = 240;
            if (y1 > 160) y1 = 160;
            if (x0 >= x1 || y0 >= y1) continue;
            args[0]=x; args[1]=y; args[2]=width; args[3]=height; args[4]=b;
            args[5]=x0; args[6]=y0; args[7]=x1; args[8]=y1; args[9]=cloudX[j]; args[10]=cloudY[j];
            if (shade(buffer + j * CLOUD_CELL_BYTES, pixels, tones, args)) touched |= 1 << j;
        }
    }
    sCloud.dirtyCells = touched | sCloud.oldShadows;
    sCloud.oldShadows = touched;
}

void KyotoCloud_Prepare(void)
{
    u32 code[KYOTO_CLOUD_CODE_BUFFER_WORDS];
    if (gMain.callback2 != CB2_Overworld)
        return;
    // Field reconstruction also completes when the destination has no clouds.
    // Keeping this latch until cloud sprites exist would black out indoor maps
    // and other weather permanently after leaving a cloud map.
    sCloud.fieldLoading = FALSE;
    sCloud.holdBlack = FALSE;
    if (!sCloud.created || gWeatherPtr->currWeather != WEATHER_SUNNY_CLOUDS)
        return;
    if (sCloud.paused)
    {
        if (sCloud.tickBase == sCloud.pauseTickBase)
            sCloud.tickBase += gMain.vblankCounter1 - sCloud.pauseFrame;
        sCloud.paused = FALSE;
    }
    if (sCloud.buffer == NULL)
    {
        sCloud.buffer = AllocUnchecked(CLOUD_OBJ_BYTES);
        if (sCloud.buffer == NULL) return;
        sCloud.oldShadows = ALL_CLOUD_CELLS;
    }
    CpuCopy32(KyotoCloudShadePixels, code, (u32)KyotoCloudShadePixelsEnd - (u32)KyotoCloudShadePixels);
    PrepareShadows(code);
}

void KyotoCloud_VBlank(void)
{
    u32 base;
    UpdateShadowWindowVBlank();
    if (!sCloud.created || sCloud.buffer == NULL || gMain.callback2 != CB2_Overworld)
        return;
    base = GetSpriteTileStartByTag(GFXTAG_CLOUD);
    if (base == TAG_NONE)
        return;
    for (u32 j = 0; j < CLOUD_COUNT; j++)
        if (sCloud.dirtyCells & (1 << j))
            DmaCopy32(3, sCloud.buffer + j * CLOUD_CELL_BYTES, (void *)(OBJ_VRAM0 + base * 32 + j * CLOUD_CELL_BYTES), CLOUD_CELL_BYTES);
    sCloud.dirtyCells = 0;
}
