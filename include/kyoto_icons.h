#ifndef GUARD_KYOTO_ICONS_H
#define GUARD_KYOTO_ICONS_H
#include "pokemon_icon.h"
#define KYOTO_USE_POKESPRITE_ICONS FALSE // Native icons retain their two distinct animation frames.
#define KYOTO_ICON_FRAME_BYTES 640
void KyotoIconSetSubsprites(struct Sprite *sprite);
const u16 *KyotoIconPalette(enum Species species, bool32 shiny, u32 personality, bool32 isEgg);
// size must be a multiple of KYOTO_ICON_FRAME_BYTES (one or two poses).
void KyotoIconCopyTiles(void *dst, const u8 *src, enum Species species, enum SpeciesIconType type, bool32 shiny, u32 size);
void KyotoIconApply(struct Sprite *sprite, enum Species species, bool32 shiny, u32 personality, bool32 isEgg);
void KyotoIconApplyMon(struct Sprite *sprite, struct Pokemon *mon);
bool32 KyotoIconManaged(struct Sprite *sprite);
void KyotoIconRelease(struct Sprite *sprite);
void KyotoIconReset(bool32 freeMemory);
void KyotoIconLoadBulkPalette(void);
void KyotoIconDrawBulk(u32 windowId, enum Species species, u32 personality, bool32 shiny, bool32 isEgg, u32 x, u32 y);
#endif
