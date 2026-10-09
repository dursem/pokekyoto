// Local, warm light for overworld actors. Native object-event palettes remain
// untouched: only the rendered OAM entries select a private illuminated copy.
#include "global.h"
#include "event_object_movement.h"
#include "graphics.h"
#include "main.h"
#include "overworld.h"
#include "palette.h"
#include "sprite.h"
#include "kyoto_lighting.h"
#include "kyoto_lamp_mask.h"
#include "constants/event_objects.h"
#include "constants/rgb.h"
#include "constants/rtc.h"

extern const u32 gFieldEffectObjectPic_BallLight[];

#define LIGHT_PAL_TAG_BASE 0xD9C0
#define MAX_LOCAL_LIGHTS 16
#define LIGHT_RAMP_STEP 12

struct ActorLight
{
    u16 strength;
    u8 slotPlusOne;
    u8 sourceSlot;
    u8 spriteId;
};
struct LampPosition { s16 x, y; };
static EWRAM_DATA struct ActorLight sActorLights[OBJECT_EVENTS_COUNT] = {0};

static void ReleaseLight(u32 objectId)
{
    struct ActorLight *state = &sActorLights[objectId];
    if (state->slotPlusOne)
    {
        u32 slot = state->slotPlusOne - 1;
        // Restore any last field OAM before handing ownership to another UI.
        if (IndexOfSpritePaletteTag(LIGHT_PAL_TAG_BASE + objectId) == slot)
        {
            for (u32 i = 0; i < 128; i++)
                if (gMain.oamBuffer[i].paletteNum == slot)
                    gMain.oamBuffer[i].paletteNum = state->sourceSlot;
            FreeSpritePaletteByTag(LIGHT_PAL_TAG_BASE + objectId);
        }
    }
    memset(state, 0, sizeof(*state));
}

void KyotoLight_Reset(void)
{
    KyotoLamp_Finish(0);
    for (u32 i = 0; i < OBJECT_EVENTS_COUNT; i++)
        ReleaseLight(i);
}

static u32 SampleLamp(s32 x, s32 y)
{
    u32 offset;
    if (x < 0 || x >= 32 || y < 0 || y >= 32)
        return 0;
    offset = ((y / 8) * 4 + x / 8) * 32 + (y & 7) * 4 + (x & 7) / 2;
    // The original lamp art has an ordered 1..15 dark-to-bright ramp.
    return (((const u8 *)gFieldEffectObjectPic_BallLight)[offset] >> ((x & 1) * 4)) & 15;
}

static u32 ActorStrength(const struct Sprite *sprite, const struct LampPosition *lamps, u32 count)
{
    s32 x = sprite->x + sprite->x2;
    s32 y = sprite->y + sprite->y2;
    s32 halfWidth = -sprite->centerToCornerVecX;
    s32 halfHeight = -sprite->centerToCornerVecY;
    u32 strength = 0;
    if (sprite->coordOffsetEnabled)
    {
        x += gSpriteCoordOffsetX;
        y += gSpriteCoordOffsetY;
    }
    for (u32 i = 0; i < count; i++)
    {
        s32 dx = x - lamps[i].x + 16;
        s32 dy = y - lamps[i].y + 16;
        u32 sum = SampleLamp(dx, dy) * 2;
        sum += SampleLamp(dx, dy - halfHeight / 2);
        sum += SampleLamp(dx, dy + halfHeight / 2);
        sum += SampleLamp(dx - halfWidth / 2, dy);
        sum += SampleLamp(dx + halfWidth / 2, dy);
        strength += sum * 256 / 90;
    }
    return min(strength, 256);
}

static u32 MixChannel(u32 a, u32 b, u32 amount)
{
    return (a * (256 - amount) + b * amount + 128) >> 8;
}

static u16 LitColor(u16 source, u16 daylight, u32 amount)
{
    u32 r = daylight & 31;
    u32 g = (daylight >> 5) & 31;
    u32 b = (daylight >> 10) & 31;
    // Lift the night tint toward the original colors, with a warm lamp cast.
    r += (31 - r) / 5;
    g += (31 - g) / 10;
    b = b * 7 / 8;
    // Use the same screen fade as the source, never flash bright during a warp
    // or Bag/Party transition. Hardware fades already cover all OBJ pixels.
    if (gPaletteFade.mode == 0 || gPaletteFade.mode == 3)
    {
        u32 fade = gPaletteFade.y * 16;
        r = MixChannel(r, gPaletteFade.blendColor & 31, fade);
        g = MixChannel(g, (gPaletteFade.blendColor >> 5) & 31, fade);
        b = MixChannel(b, (gPaletteFade.blendColor >> 10) & 31, fade);
    }
    return RGB(MixChannel(source & 31, r, amount),
               MixChannel((source >> 5) & 31, g, amount),
               MixChannel((source >> 10) & 31, b, amount));
}

static void LightActor(u32 objectId, const struct LampPosition *lamps, u32 count)
{
    struct ObjectEvent *object = &gObjectEvents[objectId];
    struct ActorLight *state = &sActorLights[objectId];
    struct Sprite *sprite;
    u32 target, slot, source, tileCount;
    if (!object->active || object->inanimate || object->spriteId >= MAX_SPRITES)
    {
        ReleaseLight(objectId);
        return;
    }
    sprite = &gSprites[object->spriteId];
    if (!sprite->inUse || sprite->invisible || sprite->oam.bpp != ST_OAM_4BPP)
    {
        ReleaseLight(objectId);
        return;
    }
    source = sprite->oam.paletteNum;
    if (state->slotPlusOne && (state->spriteId != object->spriteId || state->sourceSlot != source))
        ReleaseLight(objectId);
    target = ActorStrength(sprite, lamps, count);
    if (state->strength < target)
        state->strength = min(state->strength + LIGHT_RAMP_STEP, target);
    else if (state->strength > target)
        state->strength = max((s32)state->strength - LIGHT_RAMP_STEP, (s32)target);
    if (!state->strength)
    {
        ReleaseLight(objectId);
        return;
    }
    slot = IndexOfSpritePaletteTag(LIGHT_PAL_TAG_BASE + objectId);
    if (slot == 0xFF)
        slot = AllocSpritePalette(LIGHT_PAL_TAG_BASE + objectId);
    if (slot == 0xFF)
        return; // Never evict another sprite/weather/UI palette.
    state->slotPlusOne = slot + 1;
    state->sourceSlot = source;
    state->spriteId = object->spriteId;
    for (u32 i = 0; i < 16; i++)
    {
        u16 color = LitColor(gPlttBufferFaded[OBJ_PLTT_ID(source) + i],
                            gPlttBufferUnfaded[OBJ_PLTT_ID(source) + i], state->strength);
        gPlttBufferUnfaded[OBJ_PLTT_ID(slot) + i] = color;
        gPlttBufferFaded[OBJ_PLTT_ID(slot) + i] = color;
    }
    tileCount = GetObjectEventGraphicsInfo(object->graphicsId)->size / TILE_SIZE_4BPP;
    for (u32 i = 0; i < 128; i++)
    {
        struct OamData *oam = &gMain.oamBuffer[i];
        s32 dx = (oam->x - sprite->oam.x) & 511;
        s32 dy = (oam->y - sprite->oam.y) & 255;
        // Position + tile range also identifies split object-event pieces.
        // Another NPC sharing the native palette keeps its own lighting.
        if (oam->paletteNum == source
         && oam->tileNum >= sprite->oam.tileNum
         && oam->tileNum < sprite->oam.tileNum + tileCount
         && dx < -sprite->centerToCornerVecX * 2
         && dy < -sprite->centerToCornerVecY * 2)
            oam->paletteNum = slot;
    }
}

void KyotoLight_Prepare(void)
{
    struct LampPosition lamps[MAX_LOCAL_LIGHTS];
    u32 count = 0;
    if (gMain.callback2 != CB2_Overworld)
    {
        KyotoLight_Reset();
        return;
    }
    if (gTimeOfDay == TIME_NIGHT)
        for (u32 i = 0; i < MAX_SPRITES && count < MAX_LOCAL_LIGHTS; i++)
        {
            struct Sprite *sprite = &gSprites[i];
            if (sprite->inUse && !sprite->invisible && sprite->callback == UpdateLightSprite
             && sprite->data[5] == LIGHT_TYPE_BALL)
            {
                KyotoLamp_Prepare(sprite, count);
                lamps[count].x = sprite->x + sprite->x2 + gSpriteCoordOffsetX;
                lamps[count].y = sprite->y + sprite->y2 + gSpriteCoordOffsetY;
                count++;
            }
        }
    KyotoLamp_Finish(count);
    // Reserve the player's copy first if a very crowded map is palette-bound.
    for (u32 i = 0; i < OBJECT_EVENTS_COUNT; i++)
        if (gObjectEvents[i].isPlayer)
            LightActor(i, lamps, count);
    for (u32 i = 0; i < OBJECT_EVENTS_COUNT; i++)
        if (!gObjectEvents[i].isPlayer)
            LightActor(i, lamps, count);
}
