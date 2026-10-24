#ifndef GUARD_FIELD_WEATHER_PETALS_H
#define GUARD_FIELD_WEATHER_PETALS_H

#include "global.h"

// Ordinary (not blend-immune) OBJ palette: native time and screen fades apply.
#define KYOTO_PETALS_PALETTE_TAG 0x1210
#define KYOTO_SAKURA_PETAL_COUNT 24
#define KYOTO_AUTUMN_LEAF_COUNT 8

void Sakura_InitVars(void);
void Sakura_Main(void);
void Sakura_InitAll(void);
bool8 Sakura_Finish(void);
void AutumnWind_InitVars(void);
void AutumnWind_Main(void);
void AutumnWind_InitAll(void);
bool8 AutumnWind_Finish(void);

// Call before ResetSpriteData, or when leaving the overworld callback.
// Snapshots motion + drain state and destroys only resources owned by this effect.
void Petals_Reset(void);
// Resume an interrupted outgoing drain after menus before initializing weather.
u8 Petals_GetResumeWeather(u8 targetWeather);
// Keep Sakura's OBJ colors synchronized with the visible pink tree palette.
void Petals_BeforePaletteTransfer(void);

#endif
