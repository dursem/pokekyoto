/* FRLG PC/bag sprites and animations. */
#include "global.h"
#include "sprite.h"
#include "palette.h"
#include "bg.h"
#include "window.h"
#include "item_icon.h"
#include "text.h"
#include "string_util.h"
#include "international_string_util.h"
#include "frlg_pc.h"
#include "decompress.h"
#include "graphics.h"

#include "constants/items.h"


static const u32 sFrlgPcBagMaleGfx[] = INCBIN_U32("graphics/frlg_pc/bag_male.4bpp.lz");
static const u32 sFrlgPcBagFemaleGfx[] = INCBIN_U32("graphics/frlg_pc/bag_female.4bpp.lz");
static const u16 sFrlgPcBagPal[] = INCBIN_U16("graphics/frlg_pc/bag.gbapal");
static const u32 sFrlgPcSwapGfx[] = INCBIN_U32("graphics/frlg_pc/swap.4bpp.lz");
static const u16 sFrlgPcSwapPal[] = INCBIN_U16("graphics/frlg_pc/swap.gbapal");
const u16 gFrlgPcMenuPalette[] = INCBIN_U16("graphics/frlg_pc/menu.gbapal");
const u16 gFrlgPcDescriptionPalette[] = INCBIN_U16("graphics/frlg_pc/description.gbapal");

enum {
    TAG_BAG = 100,
    TAG_SWAP_LINE,
    TAG_ITEM_ICON,
    TAG_ITEM_ICON_ALT,
};

#define NUM_SWAP_LINE_SPRITES 9

// Indexes for sFrlgPcIconSpriteIds
enum {
    SPR_BAG,
    SPR_SWAP_LINE_START,
    SPR_ITEM_ICON = SPR_SWAP_LINE_START + NUM_SWAP_LINE_SPRITES,
    SPR_ITEM_ICON_ALT,
    SPR_COUNT
};

enum {
    ANIM_SWAP_LINE_START,
    ANIM_SWAP_LINE_MID,
    ANIM_SWAP_LINE_END,
};

enum {
    AFFINEANIM_BAG_IDLE,
    AFFINEANIM_BAG_SHAKE,
};

static EWRAM_DATA u8 sFrlgPcIconSpriteIds[SPR_COUNT] = {0};



static void SpriteCB_BagVisualSwitchingPockets(struct Sprite *sprite);
static void SpriteCB_ShakeBagSprite(struct Sprite *sprite);

static const struct OamData sOamData_Bag = {
    .affineMode = ST_OAM_AFFINE_NORMAL,
    .shape = SPRITE_SHAPE(64x64),
    .size = SPRITE_SIZE(64x64),
    .priority = 1,
    .paletteNum = 0
};

static const union AnimCmd sAnim_Bag_OpenPokeBallsPocket[] = {
    ANIMCMD_FRAME(   0, 5),
    ANIMCMD_FRAME(0x40, 0),
    ANIMCMD_END
};

static const union AnimCmd sAnim_Bag_OpenItemsPocket[] = {
    ANIMCMD_FRAME(   0, 5),
    ANIMCMD_FRAME(0x80, 0),
    ANIMCMD_END
};

static const union AnimCmd sAnim_Bag_OpenKeyItemsPocket[] = {
    ANIMCMD_FRAME(   0, 5),
    ANIMCMD_FRAME(0xc0, 0),
    ANIMCMD_END
};

static const union AnimCmd *const sAnims_Bag[] = {
    [0]      = sAnim_Bag_OpenItemsPocket,
    [1]  = sAnim_Bag_OpenKeyItemsPocket,
    [2] = sAnim_Bag_OpenPokeBallsPocket,
};

static const union AffineAnimCmd sAffineAnim_BagIdle[] = {
    AFFINEANIMCMD_FRAME(0x100, 0x100, 0, 0),
    AFFINEANIMCMD_END
};

static const union AffineAnimCmd sAffineAnim_BagShake[] = {
    AFFINEANIMCMD_FRAME(0, 0, -2, 2),
    AFFINEANIMCMD_FRAME(0, 0,  2, 4),
    AFFINEANIMCMD_FRAME(0, 0, -2, 4),
    AFFINEANIMCMD_FRAME(0, 0,  2, 2),
    AFFINEANIMCMD_END
};

static const union AffineAnimCmd *const sAffineAnimTable_Bag[] = {
    [AFFINEANIM_BAG_IDLE]  = sAffineAnim_BagIdle,
    [AFFINEANIM_BAG_SHAKE] = sAffineAnim_BagShake
};

const struct CompressedSpriteSheet gFrlgPcBagMaleSheet = {
    .data = sFrlgPcBagMaleGfx,
    .size = 0x2000,
    .tag = TAG_BAG
};

const struct CompressedSpriteSheet gFrlgPcBagFemaleSheet = {
    .data = sFrlgPcBagFemaleGfx,
    .size = 0x2000,
    .tag = TAG_BAG
};

const struct SpritePalette gFrlgPcBagPalette = {
    .data = sFrlgPcBagPal,
    .tag = TAG_BAG
};

static const struct SpriteTemplate sSpriteTemplate_Bag = {
    .tileTag = TAG_BAG,
    .paletteTag = TAG_BAG,
    .oam = &sOamData_Bag,
    .anims = sAnims_Bag,
    .images = NULL,
    .affineAnims = sAffineAnimTable_Bag,
    .callback = SpriteCallbackDummy
};

static const struct OamData sOamData_SwapLine = {
    .affineMode = ST_OAM_AFFINE_OFF,
    .shape = SPRITE_SHAPE(16x16),
    .size = SPRITE_SIZE(16x16),
    .priority = 1,
    .paletteNum = 1
};

static const union AnimCmd sAnim_SwapLine_Start[] = {
    ANIMCMD_FRAME(0, 0),
    ANIMCMD_END
};

static const union AnimCmd sAnim_SwapLine_Mid[] = {
    ANIMCMD_FRAME(4, 0),
    ANIMCMD_END
};

static const union AnimCmd sAnim_SwapLine_End[] = {
    ANIMCMD_FRAME(0, 0, .hFlip = TRUE),
    ANIMCMD_END
};

static const union AnimCmd *const sAnims_SwapLine[] = {
    [ANIM_SWAP_LINE_START] = sAnim_SwapLine_Start,
    [ANIM_SWAP_LINE_MID]   = sAnim_SwapLine_Mid,
    [ANIM_SWAP_LINE_END]   = sAnim_SwapLine_End
};

const struct CompressedSpriteSheet gFrlgPcSwapSheet = {
    .data = sFrlgPcSwapGfx,
    .size = 0x100,
    .tag = TAG_SWAP_LINE
};

const struct SpritePalette gFrlgPcSwapPalette = {
    .data = sFrlgPcSwapPal,
    .tag = TAG_SWAP_LINE
};

static const struct SpriteTemplate sSpriteTemplate_SwapLine = {
    .tileTag = TAG_SWAP_LINE,
    .paletteTag = TAG_SWAP_LINE,
    .oam = &sOamData_SwapLine,
    .anims = sAnims_SwapLine,
    .images = NULL,
    .affineAnims = gDummySpriteAffineAnimTable,
    .callback = SpriteCallbackDummy
};

static const struct OamData sOamData_ItemIcon = {
    .affineMode = ST_OAM_AFFINE_OFF,
    .shape = SPRITE_SHAPE(32x32),
    .size = SPRITE_SIZE(32x32),
    .priority = 1,
    .paletteNum = 2
};

static const union AnimCmd sAnim_ItemIcon[] = {
    ANIMCMD_FRAME(0, 0),
    ANIMCMD_END
};

static const union AnimCmd *const sAnims_ItemIcon[] = {
    sAnim_ItemIcon
};

static const struct SpriteTemplate sSpriteTemplate_ItemIcon = {
    .tileTag = TAG_ITEM_ICON,
    .paletteTag = TAG_ITEM_ICON,
    .oam = &sOamData_ItemIcon,
    .anims = sAnims_ItemIcon,
    .images = NULL,
    .affineAnims = gDummySpriteAffineAnimTable,
    .callback = SpriteCallbackDummy
};



void FrlgPcResetIcons(void)
{
    u16 i;

    for (i = 0; i < SPR_COUNT; i++)
        sFrlgPcIconSpriteIds[i] = SPRITE_NONE;
}

void FrlgPcCreateBag(u8 animNum)
{
    sFrlgPcIconSpriteIds[SPR_BAG] = CreateSprite(&sSpriteTemplate_Bag, 40, 68, 0);
    FrlgPcSetBagPocket(animNum);
}

void FrlgPcSetBagPocket(u8 animNum)
{
    struct Sprite *sprite = &gSprites[sFrlgPcIconSpriteIds[SPR_BAG]];
    sprite->y2 = -5;
    sprite->callback = SpriteCB_BagVisualSwitchingPockets;
    StartSpriteAnim(sprite, animNum);
}

static void SpriteCB_BagVisualSwitchingPockets(struct Sprite *sprite)
{
    if (sprite->y2 != 0)
        sprite->y2++;
    else
        sprite->callback = SpriteCallbackDummy;
}

void FrlgPcShakeBag(void)
{
    struct Sprite *sprite = &gSprites[sFrlgPcIconSpriteIds[SPR_BAG]];
    if (sprite->affineAnimEnded)
    {
        StartSpriteAffineAnim(sprite, AFFINEANIM_BAG_SHAKE);
        sprite->callback = SpriteCB_ShakeBagSprite;
    }
}

static void SpriteCB_ShakeBagSprite(struct Sprite *sprite)
{
    if (sprite->affineAnimEnded)
    {
        StartSpriteAffineAnim(sprite, AFFINEANIM_BAG_IDLE);
        sprite->callback = SpriteCallbackDummy;
    }
}

void FrlgPcCreateSwapLine(void)
{
    u8 i;
    u8 * spriteIds = &sFrlgPcIconSpriteIds[SPR_SWAP_LINE_START];

    for (i = 0; i < NUM_SWAP_LINE_SPRITES; i++)
    {
        spriteIds[i] = CreateSprite(&sSpriteTemplate_SwapLine, i * 16 + 96, 7, 0);
        switch (i)
        {
        case 0:
            // ANIM_SWAP_LINE_START, by default
            break;
        case NUM_SWAP_LINE_SPRITES - 1:
            StartSpriteAnim(&gSprites[spriteIds[i]], ANIM_SWAP_LINE_END);
            break;
        default:
            StartSpriteAnim(&gSprites[spriteIds[i]], ANIM_SWAP_LINE_MID);
            break;
        }
        gSprites[spriteIds[i]].invisible = TRUE;
    }
}

void FrlgPcSetSwapLineInvisibility(bool8 invisible)
{
    u8 i;
    u8 * spriteIds = &sFrlgPcIconSpriteIds[SPR_SWAP_LINE_START];

    for (i = 0; i < NUM_SWAP_LINE_SPRITES; i++)
        gSprites[spriteIds[i]].invisible = invisible;
}

void FrlgPcUpdateSwapLinePos(s16 x, u16 y)
{
    u8 i;
    u8 * spriteIds = &sFrlgPcIconSpriteIds[SPR_SWAP_LINE_START];

    for (i = 0; i < NUM_SWAP_LINE_SPRITES; i++)
    {
        gSprites[spriteIds[i]].x2 = x;
        gSprites[spriteIds[i]].y = y + 7;
    }
}

void FrlgPcCreateItemIcon(u16 itemId, u8 idx)
{
    u8 * spriteIds = &sFrlgPcIconSpriteIds[SPR_ITEM_ICON];
    u8 spriteId;

    if (spriteIds[idx] == SPRITE_NONE)
    {
        // Either TAG_ITEM_ICON or TAG_ITEM_ICON_ALT
        FreeSpriteTilesByTag(TAG_ITEM_ICON + idx);
        FreeSpritePaletteByTag(TAG_ITEM_ICON + idx);
        spriteId = AddItemIconSprite(TAG_ITEM_ICON + idx, TAG_ITEM_ICON + idx, itemId);
        if (spriteId != MAX_SPRITES)
        {
            spriteIds[idx] = spriteId;
            gSprites[spriteId].oam.priority = 1;
            gSprites[spriteId].x2 = 24;
            gSprites[spriteId].y2 = 140;
        }
    }
}

void FrlgPcDestroyItemIcon(u8 idx)
{
    u8 * spriteIds = &sFrlgPcIconSpriteIds[SPR_ITEM_ICON];

    if (spriteIds[idx] != SPRITE_NONE)
    {
        DestroySpriteAndFreeResources(&gSprites[spriteIds[idx]]);
        spriteIds[idx] = SPRITE_NONE;
    }
}


void FrlgPcFormatPlayerName(u8 *name)
{
    const u8 *cursor;

    // Short Latin names are valid; length alone cannot identify Japanese text.
    for (cursor = name; *cursor != EOS; cursor++)
        if ((*cursor >= CHAR_A && *cursor <= CHAR_z)
         || (*cursor >= CHAR_0 && *cursor <= CHAR_9))
        {
            StripExtCtrlCodes(name);
            return;
        }
    ConvertInternationalPlayerName(name);
}
