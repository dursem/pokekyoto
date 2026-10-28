#ifndef GUARD_BATTLE_BG_H
#define GUARD_BATTLE_BG_H

struct BattleBackground
{
    const void *tileset;
    const void *tilemap;
};

struct BattleBackgroundEntry
{
    const void *tileset;
    const void *tilemap;
};

void BattleInitBgsAndWindows(void);
void InitBattleBgsVideo(void);
void LoadBattleMenuWindowGfx(void);
void DrawMainBattleBackground(void);
// POKEKYOTO_PLATEAUS_V2_SOURCE
s16 KyotoBattlePlatformX(u32 battler, s16 defaultX);
void LoadBattleTextboxAndBackground(void);
void InitLinkBattleVsScreen(u8 taskId);
void DrawBattleEntryBackground(void);
bool8 LoadChosenBattleElement(u8 caseId);
void DrawTerrainTypeBattleBackground(void);
void LoadKyotoBattleActionUi(void);
void LoadKyotoBattleMessageUi(void);
void LoadKyotoBattleMoveUi(void);
void LoadKyotoBattleMoveTiles(void);

#endif // GUARD_BATTLE_BG_H
