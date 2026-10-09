#include "global.h"
#include "glide.h"
#include "event_object_movement.h"
#include "field_player_avatar.h"
#include "fieldmap.h"
#include "item.h"
#include "main.h"
#include "sprite.h"
#include "constants/items.h"
#include "constants/metatile_behaviors.h"
#include "constants/event_objects.h"

// Reserve this palette tag in forks that add custom sprites.
#define GLIDE_PALETTE_TAG 0x11FE
static const u8 sTiles[] = INCGFX_U8("graphics/glide/paraglider.png", ".4bpp");
static const u16 sPalette[] = INCBIN_U16("graphics/glide/paraglider.gbapal");
static const struct SpriteFrameImage sImages[] = {
    {sTiles, 2048}, {sTiles + 2048, 2048}, {sTiles + 4096, 2048}, {sTiles + 6144, 2048},
};
static const struct SpritePalette sPal = {sPalette, GLIDE_PALETTE_TAG};
static const struct OamData sOam = {
    .shape = SPRITE_SHAPE(64x64), .size = SPRITE_SIZE(64x64), .priority = 1,
};
static const union AnimCmd sSouth[] = {ANIMCMD_FRAME(0, 1), ANIMCMD_END};
static const union AnimCmd sNorth[] = {ANIMCMD_FRAME(1, 1), ANIMCMD_END};
static const union AnimCmd sWest[] = {ANIMCMD_FRAME(2, 1), ANIMCMD_END};
static const union AnimCmd sEast[] = {ANIMCMD_FRAME(3, 1), ANIMCMD_END};
static const union AnimCmd *const sAnims[] = {sSouth, sNorth, sWest, sEast};
static void SpriteCB_Glide(struct Sprite *sprite);
static const struct SpriteTemplate sTemplate = {
    .tileTag = TAG_NONE, .paletteTag = GLIDE_PALETTE_TAG,
    .oam = &sOam, .anims = sAnims, .images = sImages,
    .affineAnims = gDummySpriteAffineAnimTable, .callback = SpriteCB_Glide,
};
EWRAM_DATA static bool8 sGlideActive = FALSE;
EWRAM_DATA static bool8 sGlideReachedCloud = FALSE;
EWRAM_DATA static u8 sGlideSpriteId = 0;
EWRAM_DATA static bool8 sGlideHidPlayer = FALSE;

static struct ObjectEvent *Player(void)
{
    if (gPlayerAvatar.objectEventId >= OBJECT_EVENTS_COUNT)
        return NULL;
    return &gObjectEvents[gPlayerAvatar.objectEventId];
}
static u8 Behavior(s16 x, s16 y)
{
    return MapGridGetMetatileBehaviorAt(x, y);
}
static bool8 OnFoot(void)
{
    return (gPlayerAvatar.flags & (PLAYER_AVATAR_FLAG_ON_FOOT
        | PLAYER_AVATAR_FLAG_MACH_BIKE | PLAYER_AVATAR_FLAG_ACRO_BIKE
        | PLAYER_AVATAR_FLAG_SURFING | PLAYER_AVATAR_FLAG_UNDERWATER))
        == PLAYER_AVATAR_FLAG_ON_FOOT;
}
bool8 Glide_IsActive(void)
{
    struct ObjectEvent *p = Player();
    return sGlideActive || (p != NULL && p->active && OnFoot()
        && Behavior(p->currentCoords.x, p->currentCoords.y) == MB_GLIDE_CLOUD
        && CheckBagHasItem(ITEM_PARAGLIDER, 1));
}

static void ShowPlayer(void)
{
    struct ObjectEvent *p = Player();
    if (sGlideHidPlayer && p != NULL && p->spriteId < MAX_SPRITES)
        gSprites[p->spriteId].invisible = p->invisible;
    sGlideHidPlayer = FALSE;
}
void Glide_Reset(void)
{
    ShowPlayer();
    if (sGlideSpriteId < MAX_SPRITES && gSprites[sGlideSpriteId].inUse
        && gSprites[sGlideSpriteId].callback == SpriteCB_Glide)
        DestroySprite(&gSprites[sGlideSpriteId]);
    sGlideSpriteId = MAX_SPRITES;
    FreeSpritePaletteByTag(GLIDE_PALETTE_TAG);
    sGlideActive = FALSE;
    sGlideReachedCloud = FALSE;
}
static bool8 CreateGlider(void)
{
    if (sGlideSpriteId < MAX_SPRITES && gSprites[sGlideSpriteId].inUse
        && gSprites[sGlideSpriteId].callback == SpriteCB_Glide)
        return TRUE;
    if (LoadSpritePalette(&sPal) == 0xFF)
    {
        return FALSE;
    }
    sGlideSpriteId = CreateSpriteUnchecked(&sTemplate, 0, 0, 0);
    if (sGlideSpriteId == MAX_SPRITES)
    {
        FreeSpritePaletteByTag(GLIDE_PALETTE_TAG);
        return FALSE;
    }
    return TRUE;
}
static bool8 InsideMap(s16 x, s16 y)
{
    return x >= MAP_OFFSET && y >= MAP_OFFSET
        && x < gMapHeader.mapLayout->width + MAP_OFFSET
        && y < gMapHeader.mapLayout->height + MAP_OFFSET;
}
bool8 Glide_CanUse(void)
{
    struct ObjectEvent *p = Player();
    s16 x, y;
    if (p == NULL || !p->active || !OnFoot()
        || !CheckBagHasItem(ITEM_PARAGLIDER, 1))
        return FALSE;
    x = p->currentCoords.x; y = p->currentCoords.y;
    if (Behavior(x, y) != MB_GLIDE_LANDING)
        return FALSE;
    if (sGlideActive)
        return TRUE;
    MoveCoords(p->facingDirection, &x, &y);
    return InsideMap(x, y) && Behavior(x, y) == MB_GLIDE_CLOUD;
}
void Glide_Toggle(void)
{
    if (!Glide_CanUse()) return;
    if (sGlideActive) Glide_Reset();
    else if (CreateGlider())
    {
        sGlideActive = TRUE;
        sGlideReachedCloud = FALSE;
        Glide_Update();
    }
}
void Glide_Update(void)
{
    struct ObjectEvent *p = Player();
    u8 b;
    if (p == NULL || !p->active || !OnFoot())
    {
        if (sGlideActive) Glide_Reset();
        return;
    }
    b = Behavior(p->currentCoords.x, p->currentCoords.y);
    if (!sGlideActive && b == MB_GLIDE_CLOUD && CheckBagHasItem(ITEM_PARAGLIDER, 1))
    {
        // Derive air state after save/load and full-screen menus; no save extension.
        sGlideActive = TRUE;
        sGlideReachedCloud = TRUE;
    }
    if (!sGlideActive) return;
    if (b == MB_GLIDE_CLOUD) sGlideReachedCloud = TRUE;
    else if (sGlideReachedCloud && gPlayerAvatar.tileTransitionState != T_TILE_TRANSITION
        && p->currentCoords.x == p->previousCoords.x
        && p->currentCoords.y == p->previousCoords.y)
    {
        Glide_Reset();
        return;
    }
    if (CreateGlider()) SpriteCB_Glide(&gSprites[sGlideSpriteId]);
    // On allocation failure preserve terrain permissions and the normal player sprite.
}
static void SpriteCB_Glide(struct Sprite *sprite)
{
    struct ObjectEvent *p = Player();
    struct Sprite *hero;
    u8 dir;
    if (!sGlideActive || p == NULL || p->spriteId >= MAX_SPRITES)
    {
        sprite->invisible = TRUE;
        return;
    }
    hero = &gSprites[p->spriteId];
    dir = p->facingDirection;
    if (dir >= DIR_SOUTH && dir <= DIR_EAST)
        StartSpriteAnimIfDifferent(sprite, dir - DIR_SOUTH);
    sprite->x = hero->x;
    // Keep the feet at the same map position as the 16x32 on-foot sprite.
    sprite->y = hero->y - 16;
    sprite->x2 = hero->x2;
    sprite->y2 = hero->y2 + ((gMain.vblankCounter1 >> 4) % 3) - 1;
    sprite->coordOffsetEnabled = hero->coordOffsetEnabled;
    sprite->oam.priority = hero->oam.priority;
    sprite->subpriority = hero->subpriority;
    sprite->invisible = p->invisible;
    hero->invisible = TRUE;
    sGlideHidPlayer = TRUE;
}
bool8 Glide_GetCollision(struct ObjectEvent *object, s16 x, s16 y, enum Collision *result)
{
    u8 b = Behavior(x, y);
    u8 i;
    struct ObjectEvent *p = Player();
    if (b != MB_GLIDE_CLOUD && (object != p || !sGlideActive)) return FALSE;
    *result = COLLISION_IMPASSABLE;
    if (object != p || !sGlideActive) return TRUE;
    if (b == MB_GLIDE_LANDING) return FALSE;
    if (b != MB_GLIDE_CLOUD) return TRUE;
    // Deliberately block connections: place an explicit landing before a warp/edge.
    if (!InsideMap(x, y)) return TRUE;
    for (i = 0; i < OBJECT_EVENTS_COUNT; i++)
    {
        struct ObjectEvent *other = &gObjectEvents[i];
        if (other == p || !other->active) continue;
        if ((other->currentCoords.x == x && other->currentCoords.y == y)
            || (other->previousCoords.x == x && other->previousCoords.y == y))
        {
            *result = COLLISION_OBJECT_EVENT;
            return TRUE;
        }
    }
    *result = COLLISION_NONE;
    return TRUE;
}
