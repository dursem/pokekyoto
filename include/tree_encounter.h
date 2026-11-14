#ifndef GUARD_TREE_ENCOUNTER_H
#define GUARD_TREE_ENCOUNTER_H

// Called only from the overworld. No save block changes are required.
void TreeEncounter_OnStep(void);
void TreeEncounter_UpdateSprites(void);
void TreeEncounter_UpdatePalette(void);
void TreeEncounter_ReleaseVisuals(void);
void TreeEncounter_Reset(void);
const u8 *TreeEncounter_GetInteractionScript(s16 x, s16 y, u8 elevation);

// Field script native functions. Result is returned in VAR_RESULT.
void TreeEncounter_CreateMon(void);
void TreeEncounter_EndEffect(void);
void TreeEncounter_DebugActivate(void);
void TreeEncounter_DebugClear(void);

#endif
