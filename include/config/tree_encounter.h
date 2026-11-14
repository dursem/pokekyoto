#ifndef GUARD_CONFIG_TREE_ENCOUNTER_H
#define GUARD_CONFIG_TREE_ENCOUNTER_H

#define TREE_ENCOUNTER_ENABLED       TRUE

// Existing story progress; no save-format changes or new persistent flags.
// State 2 is written AFTER ChooseStarter returns; state 3 ends the lab dialogue.
#define TREE_ENCOUNTER_STARTER_FLAG       FLAG_SYS_POKEMON_GET
#define TREE_ENCOUNTER_STARTER_VAR        VAR_BIRCH_LAB_STATE
#define TREE_ENCOUNTER_STARTER_MIN_STATE  2
#define TREE_ENCOUNTER_MIN_STEPS     25
#define TREE_ENCOUNTER_MAX_STEPS     100
#define TREE_ENCOUNTER_RETRY_STEPS   8
#define TREE_ENCOUNTER_SEARCH_RADIUS 6
#define TREE_ENCOUNTER_MAX_DISTANCE  12

// Conservative local on-foot path check; larger detours are deliberately skipped.
#define TREE_ENCOUNTER_PATH_RADIUS   12
#define TREE_ENCOUNTER_LEAF_COUNT    5
#define TREE_ENCOUNTER_LEAF_CYCLE    336
#define TREE_ENCOUNTER_LEAF_TAG      0x7C71

// Palette 2 is the foliage palette of Kyoto's General tileset.
// Colours are copied from the currently displayed background palette;
// no tree tiles, tile animations, collisions or weather slots are replaced.
#define TREE_ENCOUNTER_TREE_PALETTE  2

#endif
