#ifndef GUARD_TREE_ENCOUNTER_SWAY_H
#define GUARD_TREE_ENCOUNTER_SWAY_H

#ifdef TREE_ENCOUNTER_HOST_TEST
// Existing reachability tests stub the GBA graphics engine. Visual behavior is
// checked separately in the mGBA test; do not change those gameplay tests.
static inline void TreeEncounterSway_Update(s16 x, s16 y, u8 shape, u16 phase)
{
    (void)x; (void)y; (void)shape; (void)phase;
}
static inline void TreeEncounterSway_Release(void) {}
#else
void TreeEncounterSway_Update(s16 x, s16 y, u8 shape, u16 phase);
void TreeEncounterSway_Release(void);
#endif

#endif
