#ifndef GUARD_FRLG_PC_H
#define GUARD_FRLG_PC_H

#include "main.h"
#include "sprite.h"

void ItemPc_Init(u8 kind, MainCallback callback);
void ItemPc_SetInitializedFlag(bool8 flag);
void FrlgPcOpenDeposit(MainCallback callback);
bool8 IsFrlgPlayerPC(void);
void BedroomPC(void);
void PrepareBedroomPC(void);
void ChooseMonToGiveItemFromPC(u16 item, MainCallback callback);
void FrlgPcTakeGivenItem(u16 item);
void ReadFrlgPcMail(struct Mail *mail, MainCallback callback, bool8 hasText);

void BeginPCScreenEffect_TurnOn(u16 xspeed, u16 yspeed, u8 priority);
void BeginPCScreenEffect_TurnOff(u16 xspeed, u16 yspeed, u8 priority);
bool8 IsPCScreenEffectRunning_TurnOn(void);
bool8 IsPCScreenEffectRunning_TurnOff(void);

extern const struct CompressedSpriteSheet gFrlgPcSwapSheet;
extern const struct SpritePalette gFrlgPcSwapPalette;
extern const struct CompressedSpriteSheet gFrlgPcBagMaleSheet;
extern const struct CompressedSpriteSheet gFrlgPcBagFemaleSheet;
extern const struct SpritePalette gFrlgPcBagPalette;
extern const u16 gFrlgPcMenuPalette[];
extern const u16 gFrlgPcDescriptionPalette[];
void FrlgPcResetIcons(void);
void FrlgPcCreateBag(u8 pocket);
void FrlgPcSetBagPocket(u8 pocket);
void FrlgPcShakeBag(void);
void FrlgPcCreateSwapLine(void);
void FrlgPcSetSwapLineInvisibility(bool8 invisible);
void FrlgPcUpdateSwapLinePos(s16 x, u16 y);
void FrlgPcCreateItemIcon(u16 itemId, u8 slot);
void FrlgPcDestroyItemIcon(u8 slot);
void FrlgPcFormatPlayerName(u8 *name);

#endif
