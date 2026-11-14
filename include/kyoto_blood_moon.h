#ifndef GUARD_KYOTO_BLOOD_MOON_H
#define GUARD_KYOTO_BLOOD_MOON_H

#define BLOOD_MOON_START_HOUR 22

void BloodMoon_RefreshSchedule(void);
u8 BloodMoon_GetEffectiveWeather(u8 weather);
void BloodMoon_SetDebugOverride(bool8 enabled);
bool32 BloodMoon_ShouldAnnounceRising(void);
void BloodMoon_MarkRisingAnnounced(void);
void BloodMoon_ResetSprites(void);
void BloodMoon_InitAll(void);
void BloodMoon_Main(void);
bool8 BloodMoon_Finish(void);
void BloodMoon_TintPalettes(u32 palettes);
void BloodMoon_PositionCloudShadows(void);
void BloodMoon_OffsetCloudMapOrigin(s32 xTiles, s32 yTiles);
u32 BloodMoon_GetCloudShadowStrength(void);
void BloodMoon_BattleStart(void);
void BloodMoon_BattleUpdate(void);
u8 BloodMoon_GetBattleGlow(void);

#endif
