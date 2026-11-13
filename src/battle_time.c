#include "global.h"
#include "battle.h"
#include "battle_main.h"
#include "battle_time.h"
#include "battle_interface.h"
#include "main.h"
#include "palette.h"
#include "sprite.h"
#include "pokemon_summary_screen.h"
#include "constants/rgb.h"

static EWRAM_DATA bool8 sTimeTintActive = FALSE;
static EWRAM_DATA u8 sComponentTone[3][32] = {};
static EWRAM_DATA u8 sShadowDarken = 0;

struct BattleTimeFrame
{
    u32 bankMask;
    u16 colors[32][16];
    u16 spriteTags[16];
};

// Prepare outside the interrupt. Publishing a complete alternate frame keeps
// VBlank bounded even during white fades and palette-heavy move animations.
static EWRAM_DATA struct BattleTimeFrame sPreparedFrames[2] = {};
static EWRAM_DATA volatile u8 sPreparedFrameIndex = 0;
static EWRAM_DATA volatile bool8 sPreparedFrameReady = FALSE;

static u32 BlendComponent(u32 value, u32 channel, const struct BlendSettings *blend)
{
    s32 target;
    if (blend->isTint)
        return min(31, (((blend->blendColor >> (channel * 8)) & 255) * value) >> 8);

    target = (blend->blendColor >> (channel * 5)) & 31;
    return value + (((target - (s32)value) * (s32)blend->coeff) >> 4);
}

void BattleTimeSetBlend(const struct TimeBlendSettings *blend)
{
    u32 channel, value;
    sPreparedFrameReady = FALSE;
    sTimeTintActive = FALSE;
    sShadowDarken = 0;
    if (blend == NULL)
        return;

    for (channel = 0; channel < 3; channel++)
    {
        for (value = 0; value < 32; value++)
        {
            s32 start = BlendComponent(value, channel, &blend->startBlend);
            s32 end = BlendComponent(value, channel, &blend->endBlend);
            s32 tone = end + (((start - end) * (s32)blend->weight) >> 8);
            sComponentTone[channel][value] = tone;
            if (tone != value)
                sTimeTintActive = TRUE;
        }
    }

    // Preserve the authored daylight shadow; deepen it gradually as light falls.
    sShadowDarken = min(4, (31 - (sComponentTone[0][31]
                         + sComponentTone[1][31] + sComponentTone[2][31]) / 3) / 4);
}

static bool32 IsUntintedPalette(u32 tag)
{
    if (tag == gSpritePal_CategoryIcons.tag)
        return TRUE;
    switch (tag)
    {
    case TAG_HEALTHBOX_PAL:
    case TAG_HEALTHBAR_PAL:
    case TAG_HEALTHBOX_PALS_1:
    case TAG_HEALTHBOX_PALS_2:
    case TAG_STATUS_SUMMARY_BAR_PAL:
    case TAG_GIMMICK_TRIGGER_PAL:
    case TAG_MEGA_INDICATOR_PAL:
    case TAG_MISC_INDICATOR_PAL:
    case TAG_TERA_INDICATOR_PAL:
    case BATTLE_TIME_SHINY_PAL_TAG:
    case 0xD720: // Ability popup / quick-ball frame / move-info frame.
    case TAG_NONE:
        return TRUE;
    default:
        // Ball/item icons and scene effects keep their own palettes and are lit.
        return FALSE;
    }
}

static inline u16 TintColor(u16 color, bool32 shadow)
{
    u32 r = sComponentTone[0][color & 31];
    u32 g = sComponentTone[1][(color >> 5) & 31];
    u32 b = sComponentTone[2][(color >> 10) & 31];
    if (shadow)
    {
        r = r * (16 - sShadowDarken) >> 4;
        g = g * (16 - sShadowDarken) >> 4;
        b = b * (16 - sShadowDarken) >> 4;
    }
    return RGB2(r, g, b);
}

static inline u16 ApplyDisplayFade(u16 color, const struct PaletteDisplayFade *fade)
{
    s32 r, g, b;
    s32 amount = fade->amount;

    // A normal fade's endpoints do not need channel arithmetic. Keeping the
    // kind switch outside the channels also bounds the battle VBlank work.
    if (fade->kind == PALETTE_DISPLAY_FADE_NORMAL)
    {
        if (amount == 16)
            return fade->color & 0x7FFF;
        if (amount == 0)
            return color & 0x7FFF;
    }

    r = color & 31;
    g = (color >> 5) & 31;
    b = (color >> 10) & 31;
    switch (fade->kind)
    {
    case PALETTE_DISPLAY_FADE_NORMAL:
        r += ((((fade->color & 31) - r) * amount) >> 4);
        g += (((((fade->color >> 5) & 31) - g) * amount) >> 4);
        b += (((((fade->color >> 10) & 31) - b) * amount) >> 4);
        break;
    case PALETTE_DISPLAY_FADE_FAST_IN_WHITE:
        r = max(r, 31 - amount);
        g = max(g, 31 - amount);
        b = max(b, 31 - amount);
        break;
    case PALETTE_DISPLAY_FADE_FAST_OUT_WHITE:
        r = min(31, r + amount);
        g = min(31, g + amount);
        b = min(31, b + amount);
        break;
    case PALETTE_DISPLAY_FADE_FAST_IN_BLACK:
        r = min(r, amount);
        g = min(g, amount);
        b = min(b, amount);
        break;
    case PALETTE_DISPLAY_FADE_FAST_OUT_BLACK:
        r = max(0, r - amount);
        g = max(0, g - amount);
        b = max(0, b - amount);
        break;
    }
    return RGB2(r, g, b);
}

void BattleTimePreparePalettes(void)
{
    u32 bank, i;
    u32 frameIndex;
    struct BattleTimeFrame *frame;

    if (!sTimeTintActive || gPaletteFade.bufferTransferDisabled
     || gMain.vblankCallback != VBlankCB_Battle)
    {
        sPreparedFrameReady = FALSE;
        return;
    }

    frameIndex = sPreparedFrameIndex ^ 1;
    frame = &sPreparedFrames[frameIndex];
    frame->bankMask = 0;

    for (bank = 8; bank < 32; bank++)
    {
        bool32 shadow = FALSE;
        bool32 hasFade;
        struct PaletteDisplayFade fade;
        // BG8-11 are battle-mon copies used by animations; OBJ0-3 are battlers.
        if (bank >= 12 && bank < 16)
            continue;
        if (bank >= 20)
        {
            u32 tag = GetSpritePaletteTagByPaletteNum(bank - 16);
            if (IsUntintedPalette(tag))
                continue;
            frame->spriteTags[bank - 16] = tag;
            shadow = tag == BATTLE_TIME_SHADOW_PAL_TAG;
        }

        hasFade = GetPaletteFadeForDisplay(bank, &fade);
        if (hasFade && fade.kind == PALETTE_DISPLAY_FADE_NORMAL && fade.amount == 0)
            hasFade = FALSE;
        // Direct palette restores used by animations can invalidate a prior
        // fade. Validate the entire bank, including its transparent color.
        for (i = 0; hasFade && i < 16; i++)
        {
            if ((gPlttBufferFaded[bank * 16 + i] & 0x7FFF)
                != ApplyDisplayFade(gPlttBufferUnfaded[bank * 16 + i], &fade))
                hasFade = FALSE;
        }
        for (i = 0; i < 16; i++)
        {
            u16 raw = gPlttBufferUnfaded[bank * 16 + i];
            u16 faded = gPlttBufferFaded[bank * 16 + i];
            // Apply the ambient light before a global fade, preserving white
            // transitions. If an animation replaced the fade, retain its color.
            if (hasFade)
                frame->colors[bank][i] = ApplyDisplayFade(TintColor(raw, shadow), &fade);
            else
                frame->colors[bank][i] = TintColor(faded, shadow);
        }
        frame->bankMask |= 1u << bank;
    }

    // Palette contents and their tags must be complete before IRQ can see the
    // new index. The old frame remains valid if VBlank interrupts preparation.
    __asm__ volatile ("" ::: "memory");
    sPreparedFrameIndex = frameIndex;
    sPreparedFrameReady = TRUE;
}

void BattleTimeTransferPalettes(void)
{
    u32 bank;
    const struct BattleTimeFrame *frame;

    if (!sPreparedFrameReady || !sTimeTintActive || gPaletteFade.bufferTransferDisabled)
        return;

    frame = &sPreparedFrames[sPreparedFrameIndex];
    for (bank = 8; bank < 32; bank++)
    {
        if (!(frame->bankMask & (1u << bank)))
            continue;
        // An interrupted game frame may have already reassigned a slot to UI.
        // Never transfer an older scene palette into a differently tagged slot.
        if (bank >= 20 && GetSpritePaletteTagByPaletteNum(bank - 16)
                        != frame->spriteTags[bank - 16])
            continue;
        CpuCopy16(frame->colors[bank], (u16 *)PLTT + bank * 16, PLTT_SIZE_4BPP);
    }
}
