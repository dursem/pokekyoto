#include "global.h"
#include "bg.h"
#include "field_weather.h"
#include "gpu_regs.h"
#include "kyoto_aurora.h"
#include "main.h"
#include "overworld.h"
#include "palette.h"
#include "script.h"
#include "constants/rtc.h"
#include "constants/weather.h"

static const u32 sTiles[] = INCBIN_U32("graphics/weather/kyoto_aurora/tiles.bin");
static const u32 sMaps[] = INCBIN_U32("graphics/weather/kyoto_aurora/maps.bin");
static const u16 sColors[] = INCBIN_U16("graphics/weather/kyoto_aurora/pal.bin");
#include "../graphics/weather/kyoto_aurora/frames.h"

enum { BANK = 14, BLOCK = 27, FIRST_TILE = 544, TILE_CAP = 160, BG_CNT = (BLOCK << 8) | (2 << 2) };
enum { STATE_TAG = 0x41555241, MAX_STRENGTH = 128 };
struct AuroraState { u32 tag, tick; u16 frame, step, strength, last; };
// Store sixteen bytes in the last, off-screen row of our private tilemap.
// Every field reset also resets the animation. VRAM writes must be 16 or 32 bits.
#define STATE ((volatile struct AuroraState *)(VRAM + (BLOCK + 1) * 2048 - sizeof(struct AuroraState)))
#define HW_PALETTE ((volatile u16 *)BG_PLTT + BANK * 16)

static void ReleaseCanvas(void)
{
    // Other screens can take ownership between callbacks.
    if (STATE->tag != STATE_TAG || REG_BG0CNT != BG_CNT)
        return;
    REG_BG0CNT = GetGpuReg(REG_OFFSET_BG0CNT);
    REG_BG0HOFS = GetGpuReg(REG_OFFSET_BG0HOFS);
    REG_BG0VOFS = GetGpuReg(REG_OFFSET_BG0VOFS);
    REG_DISPCNT = GetGpuReg(REG_OFFSET_DISPCNT);
    REG_WININ = GetGpuReg(REG_OFFSET_WININ);
    REG_WINOUT = GetGpuReg(REG_OFFSET_WINOUT);
    REG_BLDCNT = GetGpuReg(REG_OFFSET_BLDCNT);
    REG_BLDALPHA = GetGpuReg(REG_OFFSET_BLDALPHA);
    for (u32 i = 1; i < 16; i++)
        HW_PALETTE[i] = gPlttBufferFaded[BANK * 16 + i];
    STATE->tag = 0;
}

static void LoadFrame(u32 frame)
{
    const u32 *tiles = sTiles + sAuroraOffset[frame] * 8;
    u32 count = sAuroraSize[frame];
    u32 first = count < TILE_CAP ? count : TILE_CAP;
    // Both ranges are outside the map tile cache and the field's permanent UI frames.
    DmaCopy32(3, tiles, (void *)(VRAM + 0x8000 + 32 * FIRST_TILE), first * 32);
    if (count > first)
        DmaCopy32(3, tiles + first * 8, (void *)(VRAM + 0x8000 + 32 * 64), (count - first) * 32);
    DmaCopy32(3, sMaps + frame * 512, (void *)(VRAM + BLOCK * 2048), 2048 - sizeof(struct AuroraState));
}

// The GBA blends only two pixels. A light that already uses alpha must see
// the map directly, even when the curtain passes over it.
static bool32 MaskBlendedObjects(void)
{
    volatile u16 *oam = (volatile u16 *)OAM;
    s32 freeSlot = 127;
    for (s32 i = 0; i < 128; i++)
    {
        u32 attr = oam[i * 4];
        if ((attr & 0x0c00) != 0x0400 || (attr & 0x300) == 0x0200)
            continue;
        while (freeSlot > i && (oam[freeSlot * 4] & 0xff) != 160
            && (oam[freeSlot * 4] & 0x300) != 0x200)
            freeSlot--;
        if (freeSlot <= i)
            return FALSE;
        oam[freeSlot * 4 + 1] = oam[i * 4 + 1];
        oam[freeSlot * 4 + 2] = oam[i * 4 + 2];
        oam[freeSlot * 4] = (attr & ~0x0c00) | 0x0800;
        freeSlot--;
    }
    return TRUE;
}

// The weather engine polls finish until the last visible frame fades out.
// Hidden canvases (including menus and warps) never delay another weather.
bool8 KyotoAurora_Finish(void)
{
    return gMain.callback2 == CB2_Overworld && STATE->tag == STATE_TAG
        && REG_BG0CNT == BG_CNT && STATE->strength != 0;
}

void KyotoAurora_VBlank(void)
{
    if (gMain.callback2 != CB2_Overworld || GetCurrentWeather() != WEATHER_NORTHERN_LIGHTS
        || gPaletteFade.active || !MapHasNaturalLight(gMapHeader.mapType)
        || ArePlayerFieldControlsLocked() || KyotoAurora_MapPopupActive())
    {
        ReleaseCanvas();
        return;
    }
    bool32 night = gTimeOfDay == TIME_NIGHT && gWeatherPtr->nextWeather == WEATHER_NORTHERN_LIGHTS;
    if (STATE->tag != STATE_TAG || REG_BG0CNT != BG_CNT)
    {
        if (!night)
            return;
        STATE->tag = STATE_TAG;
        STATE->tick = 0;
        STATE->frame = 0;
        STATE->step = 0;
        STATE->last = 0xFFFF;
        STATE->strength = 0;
    }
    if (night && STATE->strength < MAX_STRENGTH)
        STATE->strength++;
    else if (!night && STATE->strength)
        STATE->strength--;
    if (!STATE->strength)
    {
        ReleaseCanvas();
        return;
    }
    if (GetGpuReg(REG_OFFSET_WIN0H) != 0x00ff || GetGpuReg(REG_OFFSET_WIN0V) != 0x00ff
        || GetGpuReg(REG_OFFSET_WIN1V) != 0xffff || !MaskBlendedObjects())
    {
        ReleaseCanvas();
        return;
    }
    if (++STATE->step == 10)
    {
        STATE->step = 0;
        if (++STATE->frame == ARRAY_COUNT(sAuroraSize))
            STATE->frame = 0;
    }
    if (STATE->last != STATE->frame)
    {
        LoadFrame(STATE->frame);
        STATE->last = STATE->frame;
    }
    u32 tick = ++STATE->tick;
    const u16 *palette = sColors + 16 * ((tick >> 3) & 63);
    u32 scale = STATE->strength;
    for (u32 i = 1; i < 16; i++)
    {
        u32 c = palette[i];
        u32 r = ((c & 31) * scale + 64) >> 7;
        u32 g = (((c >> 5) & 31) * scale + 64) >> 7;
        u32 b = ((c >> 10) * scale + 64) >> 7;
        r = min(31, r + 3); g = min(31, g + 4); b = min(31, b + 5);
        HW_PALETTE[i] = r | (g << 5) | (b << 10);
    }
    REG_BG0CNT = BG_CNT;
    REG_BG0HOFS = (tick >> 3) & 255;
    REG_BG0VOFS = 0;
    REG_DISPCNT = (GetGpuReg(REG_OFFSET_DISPCNT) & ~(DISPCNT_WIN0_ON | DISPCNT_WIN1_ON))
        | DISPCNT_BG0_ON | DISPCNT_OBJWIN_ON;
    REG_WINOUT = 0x3e3f;
    REG_BLDCNT = BLDCNT_TGT1_BG0 | BLDCNT_EFFECT_BLEND | BLDCNT_TGT2_BG1 | BLDCNT_TGT2_BG2
        | BLDCNT_TGT2_BG3 | BLDCNT_TGT2_OBJ | BLDCNT_TGT2_BD;
    REG_BLDALPHA = GetGpuReg(REG_OFFSET_BLDALPHA);
}
