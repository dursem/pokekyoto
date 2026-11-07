#ifndef GUARD_BATTLE_TIME_H
#define GUARD_BATTLE_TIME_H

#define BATTLE_TIME_SHADOW_PAL_TAG 0xD75A

struct TimeBlendSettings;

// NULL disables the scene tint. Capture only when the background is loaded.
void BattleTimeSetBlend(const struct TimeBlendSettings *blend);
// After the frame's callbacks: compose without changing animation buffers.
void BattleTimePreparePalettes(void);
// Battle VBlank only: transfer the complete prepared frame to hardware.
void BattleTimeTransferPalettes(void);

#endif
