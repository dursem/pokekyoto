// Original PokéSprite icon colors. PC palettes are streamed separately in HBlank.
#include "global.h"
#include "kyoto_icons.h"
#include "malloc.h"
#include "palette.h"
#include "sprite.h"
#include "graphics.h"
#include "data.h"
#include "window.h"
#include "constants/pokemon_icon.h"

#define ICON_TAG_BASE 0xDA00
static const struct Subsprite sIconPieces[] = {
    {.x = -20, .y = -16, .shape = SPRITE_SHAPE(32x32), .size = SPRITE_SIZE(32x32), .tileOffset = 0},
    {.x = 12, .y = -16, .shape = SPRITE_SHAPE(8x32), .size = SPRITE_SIZE(8x32), .tileOffset = 16},
};
static const struct SubspriteTable sIconPieceTable[] = {{ARRAY_COUNT(sIconPieces), sIconPieces}};
void KyotoIconSetSubsprites(struct Sprite *sprite)
{
    SetSubspriteTables(sprite, sIconPieceTable);
    sprite->subspriteMode = SUBSPRITES_IGNORE_PRIORITY;
}
struct IconAllocation { u8 *tiles; u16 tag; };
static EWRAM_DATA struct IconAllocation sIcons[MAX_SPRITES] = {0};

#include "data/kyoto_pokesprite.h"
static const u8 ALIGNED(4) sPokespriteData[] = INCBIN_U8("graphics/pokemon/kyoto_pokesprite/icons.bin");

static const u8 *ImportedIcon(enum Species species, enum SpeciesIconType type, bool32 shiny)
{
    u32 id;
    if (!KYOTO_USE_POKESPRITE_ICONS) return NULL;
    if (type == EGG_ICON) return NULL;
    id = sPokespriteIds[SanitizeSpeciesId(species)][type == FEMALE_ICON];
    return id ? sPokespriteData + id * 1344 + (shiny ? 672 : 0) : NULL;
}

static const u16 *NativeIconPalette(enum Species species, enum SpeciesIconType type)
{
    u32 index = gSpeciesInfo[species].iconPalIndex;
    if (type == EGG_ICON)
    {
        index = gSpeciesInfo[SPECIES_EGG].iconPalIndex;
        if (gSpeciesInfo[species].eggId != EGG_ID_NONE)
            index = gEggDatas[gSpeciesInfo[species].eggId].eggIconPalIndex;
    }
#if P_GENDER_DIFFERENCES
    else if (type == FEMALE_ICON && gSpeciesInfo[species].iconSpriteFemale)
        index = gSpeciesInfo[species].iconPalIndexFemale;
#endif
    return gMonIconPaletteTable[index].data;
}

const u16 *KyotoIconPalette(enum Species species, bool32 shiny, u32 personality, bool32 isEgg)
{
    enum SpeciesIconType type = NORMAL_ICON;
    const u8 *icon;
    species = GetIconSpecies(SanitizeSpeciesId(species), personality);
    if (isEgg) type = EGG_ICON;
#if P_GENDER_DIFFERENCES
    else if (IsPersonalityFemale(species, personality)) type = FEMALE_ICON;
#endif
    icon = ImportedIcon(species, type, shiny);
    return icon ? (const u16 *)(icon + KYOTO_ICON_FRAME_BYTES) : NativeIconPalette(species, type);
}

void KyotoIconCopyTiles(void *dst, const u8 *src, enum Species species, enum SpeciesIconType type, bool32 shiny, u32 size)
{
    const u8 *icon = ImportedIcon(species, type, shiny);
    if (!icon)
    {
        // Unsupported newer/custom species and eggs keep their original art
        // AND palette. Never recolor them against an unrelated front palette.
        if (type == FEMALE_ICON && !gSpeciesInfo[species].iconSpriteFemale)
            src = gSpeciesInfo[species].iconSprite;
        // Repack a native 32x32 pose at x=4 within our 40x32 canvas.
        CpuFill16(0, dst, size);
        for (u32 offset = 0; offset < size; offset += KYOTO_ICON_FRAME_BYTES)
            for (u32 y = 0; y < 32; y++)
                for (u32 x = 0; x < 32; x++)
                {
                    u32 a = ((y / 8) * 4 + x / 8) * 32 + (y & 7) * 4 + (x & 7) / 2;
                    u32 dx = x + 4;
                    u32 b = dx < 32 ? ((y / 8) * 4 + dx / 8) * 32 : 512 + (y / 8) * 32;
                    b += (y & 7) * 4 + (dx & 7) / 2;
                    ((u8 *)dst)[offset + b] |= ((src[(offset / KYOTO_ICON_FRAME_BYTES) * 512 + a] >> ((x & 1) * 4)) & 15) << ((dx & 1) * 4);
                }
        return;
    }
    // Upstream icons contain one pose. Repeat it for Emerald's two-frame API.
    for (u32 offset = 0; offset < size; offset += KYOTO_ICON_FRAME_BYTES)
        CpuCopy16(icon, (u8 *)dst + offset, min(KYOTO_ICON_FRAME_BYTES, size - offset));
}

static u8 AllocateIconPalette(u16 tag, struct Sprite *self)
{
    u32 i, j;
    u8 slot = AllocSpritePalette(tag);
    if (slot != 0xFF)
        return slot;
    // Menus preallocate the six original shared palettes. Reclaim only slots
    // which no live sprite uses; never evict a UI palette or another icon.
    for (i = 0; i < 6; i++)
    {
        slot = IndexOfSpritePaletteTag(POKE_ICON_BASE_PAL_TAG + i);
        if (slot == 0xFF) continue;
        for (j = 0; j < MAX_SPRITES; j++)
            if (&gSprites[j] != self && gSprites[j].inUse && gSprites[j].oam.paletteNum == slot)
                break;
        if (j == MAX_SPRITES)
        {
            FreeSpritePaletteByTag(POKE_ICON_BASE_PAL_TAG + i);
            return AllocSpritePalette(tag);
        }
    }
    return 0xFF;
}

void KyotoIconApply(struct Sprite *sprite, enum Species species, bool32 shiny, u32 personality, bool32 isEgg)
{
    u32 id, slot;
    enum SpeciesIconType type = NORMAL_ICON;
    const u8 *source;
    if (sprite < gSprites || sprite >= gSprites + MAX_SPRITES || !sprite->inUse || sprite->usingSheet)
        return;
    id = sprite - gSprites;
    species = GetIconSpecies(SanitizeSpeciesId(species), personality);
    if (isEgg) type = EGG_ICON;
#if P_GENDER_DIFFERENCES
    else if (IsPersonalityFemale(species, personality)) type = FEMALE_ICON;
#endif
    slot = IndexOfSpritePaletteTag(ICON_TAG_BASE + id);
    if (slot == 0xFF) slot = AllocateIconPalette(ICON_TAG_BASE + id, sprite);
    if (slot == 0xFF) return; // Resource exhaustion: keep the original valid icon.
    if (!sIcons[id].tiles) sIcons[id].tiles = Alloc(2 * KYOTO_ICON_FRAME_BYTES);
    if (!sIcons[id].tiles) { FreeSpritePaletteByTag(ICON_TAG_BASE + id); return; }
    sIcons[id].tag = ICON_TAG_BASE + id;
    source = GetMonIconPtrIsEgg(species, personality, isEgg);
    KyotoIconCopyTiles(sIcons[id].tiles, source, species, type, shiny, 2 * KYOTO_ICON_FRAME_BYTES);
    KyotoIconSetSubsprites(sprite);
    sprite->images = (const struct SpriteFrameImage *)sIcons[id].tiles;
    sprite->oam.paletteNum = slot;
    LoadPalette(KyotoIconPalette(species, shiny, personality, isEgg), OBJ_PLTT_ID(slot), 32);
    sprite->animCmdIndex = 0;
    sprite->animDelayCounter = 0;
    UpdateMonIconFrame(sprite);
}

void KyotoIconApplyMon(struct Sprite *sprite, struct Pokemon *mon)
{
    KyotoIconApply(sprite, GetMonData(mon, MON_DATA_SPECIES), GetMonData(mon, MON_DATA_IS_SHINY),
                   GetMonData(mon, MON_DATA_PERSONALITY), GetMonData(mon, MON_DATA_IS_EGG));
}
bool32 KyotoIconManaged(struct Sprite *sprite) { return sIcons[sprite - gSprites].tiles != NULL; }
void KyotoIconRelease(struct Sprite *sprite)
{
    struct IconAllocation *icon = &sIcons[sprite - gSprites];
    if (icon->tiles) { Free(icon->tiles); icon->tiles = NULL; FreeSpritePaletteByTag(icon->tag); }
}
void KyotoIconReset(bool32 freeMemory)
{
    u32 i;
    for (i = 0; i < MAX_SPRITES; i++)
    {
        if (freeMemory) KyotoIconRelease(&gSprites[i]);
        sIcons[i].tiles = NULL;
    }
}

// The bulk-selection overlay has six shared BG banks. Keep the new artwork
// while selecting; its normal/shiny colors are fitted only to this BG palette.
static const u16 sBulkPalette[] = INCBIN_U16("graphics/pokemon/kyoto_pokesprite/bulk_palette.bin");
void KyotoIconLoadBulkPalette(void)
{
    LoadPalette(sBulkPalette, BG_PLTT_ID(8), sizeof(sBulkPalette));
}

void KyotoIconDrawBulk(u32 windowId, enum Species species, u32 personality, bool32 shiny, bool32 isEgg, u32 x, u32 y)
{
    u8 ALIGNED(4) tiles[KYOTO_ICON_FRAME_BYTES];
    u8 map[16] = {0};
    enum SpeciesIconType type = NORMAL_ICON;
    const u16 *pal;
    struct Window *window = &gWindows[windowId];
    species = GetIconSpecies(species, personality);
    if (isEgg) type = EGG_ICON;
#if P_GENDER_DIFFERENCES
    else if (IsPersonalityFemale(species, personality)) type = FEMALE_ICON;
#endif
    KyotoIconCopyTiles(tiles, GetMonIconPtrIsEgg(species, personality, isEgg), species, type, shiny, sizeof(tiles));
    pal = KyotoIconPalette(species, shiny, personality, isEgg);
    for (u32 i = 1; i < 16; i++)
    {
        u32 best = 0xFFFFFFFF;
        for (u32 j = 0; j < 96; j++)
        {
            s32 dr = (pal[i]&31) - (sBulkPalette[j]&31);
            s32 dg = ((pal[i]>>5)&31) - ((sBulkPalette[j]>>5)&31);
            s32 db = ((pal[i]>>10)&31) - ((sBulkPalette[j]>>10)&31);
            u32 distance = dr*dr + dg*dg + db*db;
            if (distance < best) { best = distance; map[i] = 128 + j; }
        }
    }
    for (u32 py = 0; py < 32 && y + py < window->window.height * 8; py++)
        for (u32 px = 0; px < 40 && x + px < window->window.width * 8; px++)
        {
            u32 offset = px < 32 ? ((py/8)*4 + px/8)*32 : 512 + (py/8)*32;
            offset += (py&7)*4 + (px&7)/2;
            u32 color = (tiles[offset] >> ((px&1)*4)) & 15;
            u32 dx = x + px, dy = y + py;
            if (color)
                window->tileData[((dy/8)*window->window.width + dx/8)*64 + (dy&7)*8 + (dx&7)] = map[color];
        }
}
